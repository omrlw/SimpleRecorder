using System.Runtime.InteropServices;
using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;
using SimpleRecorder.Infrastructure.Settings;
using SimpleRecorder.Infrastructure.SourceSelection;

namespace SimpleRecorder.Infrastructure.Native;

public sealed class NativeRecorderController : IRecorderController, IDisposable
{
    private readonly ManagedNativeStubBackend _stubBackend = new();
    private readonly NativeMethods.SrStatusCallback _statusCallback;
    private nint _engineHandle;
    private bool _isUsingNative;
    private CaptureSourceDescriptor? _activeSource;
    private RecordingOptions? _activeOptions;
    private DateTimeOffset? _startedAtUtc;
    private string? _plannedRecordingSessionPath;

    public NativeRecorderController()
    {
        _statusCallback = HandleNativeStatus;
        _stubBackend.StatusChanged += (_, snapshot) =>
        {
            if (!_isUsingNative)
            {
                Publish(snapshot);
            }
        };
    }

    public event EventHandler<RecorderStatusSnapshot>? StatusChanged;

    public RecorderStatusSnapshot CurrentStatus { get; private set; } =
        new(RecorderState.Idle, null, Message: "Recorder not initialized.");

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (TryInitializeNative())
        {
            return Task.CompletedTask;
        }

        return _stubBackend.InitializeAsync(cancellationToken);
    }

    public Task StartAsync(
        CaptureSourceDescriptor source,
        RecordingOptions options,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var normalizedSource = NativeStructMapper.NormalizeSource(source);
        var normalizedOptions = NativeStructMapper.NormalizeOptions(options);
        ValidateStartSource(normalizedSource);

        _activeSource = normalizedSource;
        _activeOptions = normalizedOptions;

        if (!_isUsingNative)
        {
            return _stubBackend.StartAsync(normalizedSource, normalizedOptions, cancellationToken);
        }

        _plannedRecordingSessionPath = FileNamePolicy.BuildRecordingSessionPath(
            normalizedOptions.SaveDirectory,
            normalizedSource.Kind);
        ThrowIfNativeCallFailed(NativeMethods.PrepareRecordingOutput(_engineHandle, _plannedRecordingSessionPath));
        ThrowIfNativeCallFailed(NativeMethods.Start(
            _engineHandle,
            NativeStructMapper.ToNativeSource(normalizedSource),
            NativeStructMapper.ToNativeOptions(normalizedOptions)));

        return Task.CompletedTask;
    }

    public Task PauseAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!_isUsingNative)
        {
            return _stubBackend.PauseAsync(cancellationToken);
        }

        ThrowIfNativeCallFailed(NativeMethods.Pause(_engineHandle));
        return Task.CompletedTask;
    }

    public Task ResumeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!_isUsingNative)
        {
            return _stubBackend.ResumeAsync(cancellationToken);
        }

        ThrowIfNativeCallFailed(NativeMethods.Resume(_engineHandle));
        return Task.CompletedTask;
    }

    public Task<RecordingResult> StopAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var effectiveOptions = _activeOptions ?? new AppSettings().ToRecordingOptions();
        if (!_isUsingNative)
        {
            return _stubBackend.StopAsync(effectiveOptions, cancellationToken);
        }

        var startedAtUtc = _startedAtUtc;
        var recordingSessionPath = _plannedRecordingSessionPath;
        var outputPath = recordingSessionPath is null
            ? null
            : FileNamePolicy.BuildRecordingVideoPathFromSessionPath(recordingSessionPath);
        var duration = startedAtUtc.HasValue ? DateTimeOffset.UtcNow - startedAtUtc.Value : TimeSpan.Zero;
        Exception? stopException = null;
        var stopResult = NativeMethods.SrResultCode.Ok;
        try
        {
            stopResult = (NativeMethods.SrResultCode)NativeMethods.Stop(_engineHandle);
        }
        catch (Exception ex)
        {
            stopException = ex;
            stopResult = NativeMethods.SrResultCode.InvalidState;
        }

        var telemetry = recordingSessionPath is null
            ? null
            : NativeRecordingManifestReader.TryReadTelemetry(recordingSessionPath);
        var outputFileExists = outputPath is not null && File.Exists(outputPath);

        _startedAtUtc = null;
        _plannedRecordingSessionPath = null;

        var wasSuccessful = stopException is null && stopResult == NativeMethods.SrResultCode.Ok;
        if (wasSuccessful && !outputFileExists)
        {
            wasSuccessful = false;
        }

        var message = wasSuccessful
            ? BuildCompletedMessage(outputPath!, telemetry)
            : BuildStopFailedMessage(outputPath, stopResult, telemetry, stopException, outputFileExists);

        Publish(new RecorderStatusSnapshot(
            RecorderState.SourceSelected,
            _activeSource,
            CurrentOutputPath: wasSuccessful ? outputPath : null,
            Message: message,
            StartedAtUtc: null,
            Telemetry: telemetry));

        return Task.FromResult(new RecordingResult(
            wasSuccessful,
            wasSuccessful ? outputPath : null,
            duration,
            ErrorMessage: wasSuccessful ? null : message,
            Telemetry: telemetry));
    }

    public void Dispose()
    {
        if (_engineHandle != nint.Zero)
        {
            NativeMethods.Destroy(_engineHandle);
            _engineHandle = nint.Zero;
        }
    }

    private bool TryInitializeNative()
    {
        try
        {
            if (NativeMethods.GetAbiVersion() != NativeMethods.CurrentAbiVersion)
            {
                return false;
            }

            if (_engineHandle == nint.Zero)
            {
                _engineHandle = NativeMethods.Create();
                if (_engineHandle == nint.Zero)
                {
                    return false;
                }

                var callbackPointer = Marshal.GetFunctionPointerForDelegate(_statusCallback);
                ThrowIfNativeCallFailed(NativeMethods.SetCallback(_engineHandle, callbackPointer, nint.Zero));
            }

            ThrowIfNativeCallFailed(NativeMethods.Initialize(_engineHandle));
            _isUsingNative = true;
            return true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            _isUsingNative = false;
            if (_engineHandle != nint.Zero)
            {
                NativeMethods.Destroy(_engineHandle);
                _engineHandle = nint.Zero;
            }

            return false;
        }
    }

    private void HandleNativeStatus(nint _, NativeMethods.SrStatusEvent status)
    {
        var nativeState = (NativeMethods.SrRecorderState)status.State;
        var contractState = NativeStructMapper.ToContractState(nativeState);
        var sourceKind = Enum.IsDefined(typeof(CaptureSourceKind), status.ActiveSourceKind)
            ? (CaptureSourceKind)status.ActiveSourceKind
            : CaptureSourceKind.Display;

        if (_activeSource is null || _activeSource.Kind != sourceKind)
        {
            _activeSource = NativeStructMapper.CreateSourceStub(sourceKind);
        }

        if (contractState == RecorderState.Recording && !_startedAtUtc.HasValue)
        {
            _startedAtUtc = DateTimeOffset.UtcNow;
        }

        if (contractState is RecorderState.Idle or RecorderState.SourceSelected)
        {
            _startedAtUtc = null;
        }

        var telemetry = contractState switch
        {
            RecorderState.SourceSelected when CurrentStatus.State == RecorderState.SourceSelected => CurrentStatus.Telemetry,
            _ => null
        };

        Publish(new RecorderStatusSnapshot(
            contractState,
            _activeSource,
            CurrentOutputPath: null,
            Message: BuildRuntimeMessage(contractState),
            CountdownRemainingSeconds: status.CountdownRemainingSeconds,
            StartedAtUtc: _startedAtUtc,
            Telemetry: telemetry));
    }

    private void Publish(RecorderStatusSnapshot snapshot)
    {
        CurrentStatus = snapshot;
        StatusChanged?.Invoke(this, snapshot);
    }

    private static void ThrowIfNativeCallFailed(int result)
    {
        if ((NativeMethods.SrResultCode)result == NativeMethods.SrResultCode.Ok)
        {
            return;
        }

        throw new InvalidOperationException($"The native recorder returned {(NativeMethods.SrResultCode)result}.");
    }

    private static void ValidateStartSource(CaptureSourceDescriptor source)
    {
        if (source.Kind != CaptureSourceKind.Region)
        {
            return;
        }

        var region = source.Region ?? source.Bounds;
        if (region is null || !region.MeetsMinimumSize)
        {
            throw new InvalidOperationException(
                $"Region capture requires a valid region of at least {ScreenRegion.MinimumDimension}x{ScreenRegion.MinimumDimension} physical pixels.");
        }
    }

    private string BuildRuntimeMessage(RecorderState state)
    {
        if (state == RecorderState.Recording && _activeOptions is not null)
        {
            var source = _activeSource ?? CaptureSourceDescriptor.CreateStub(CaptureSourceKind.Display);
            var preview = CaptureSourcePreviewCalculator.Describe(source, _activeOptions);
            if (source.Kind != CaptureSourceKind.Region)
            {
                return
                    $"Recording {source.DisplayName}. Requested bounds {preview.CaptureSizeLabel} @ ({preview.CaptureRegion.X}, {preview.CaptureRegion.Y}); effective output depends on the active backend and will be reported after stop.";
            }

            return
                $"Recording {source.DisplayName}. Bounds {preview.CaptureSizeLabel} @ ({preview.CaptureRegion.X}, {preview.CaptureRegion.Y}); output {preview.OutputSizeLabel}.";
        }

        return NativeStructMapper.BuildMessage(state, _activeSource);
    }

    private static string BuildCompletedMessage(string outputPath, RecordingSessionTelemetry? telemetry)
    {
        if (telemetry is null)
        {
            return $"Video exported to {Path.GetFileName(outputPath)}.";
        }

        var encodeMode = telemetry.IsHardwareEncode ? "hardware" : "software/unknown";
        var fallbackDetail = BuildFallbackDetail(telemetry);
        return
            $"Video exported to {Path.GetFileName(outputPath)} via {telemetry.CaptureBackend} + {telemetry.EncodeBackend} ({encodeMode}) at {telemetry.AverageFramesPerSecond:F1} FPS avg, {telemetry.OutputWidth}x{telemetry.OutputHeight}, {telemetry.DroppedFrames} dropped{fallbackDetail}.";
    }

    private static string BuildStopFailedMessage(
        string? outputPath,
        NativeMethods.SrResultCode result,
        RecordingSessionTelemetry? telemetry,
        Exception? exception = null,
        bool outputFileExists = false)
    {
        var outputName = string.IsNullOrWhiteSpace(outputPath)
            ? "the recording output"
            : Path.GetFileName(outputPath);
        var failureReason = exception is null
            ? result == NativeMethods.SrResultCode.Ok && !outputFileExists
                ? "native stop reported success but no output file was found"
                : $"native result {result}"
            : $"native stop exception {exception.GetBaseException().Message}";

        if (telemetry is null)
        {
            return $"Recording stop failed with {failureReason}. {outputName} may be incomplete; no telemetry manifest was available.";
        }

        var fallbackDetail = BuildFallbackDetail(telemetry);
        var captureFailureDetail = BuildCaptureBackendFailureDetail(telemetry);
        return
            $"Recording stop failed with {failureReason}. {outputName} may be incomplete; telemetry: {telemetry.CaptureBackend} + {telemetry.EncodeBackend}, {telemetry.AverageFramesPerSecond:F1} FPS avg, {telemetry.OutputWidth}x{telemetry.OutputHeight}, {telemetry.DroppedFrames} dropped{fallbackDetail}{captureFailureDetail}.";
    }

    private static string BuildFallbackDetail(RecordingSessionTelemetry telemetry)
    {
        if (string.Equals(telemetry.CaptureFallbackReason, "none", StringComparison.OrdinalIgnoreCase) ||
            string.IsNullOrWhiteSpace(telemetry.CaptureFallbackTo))
        {
            return string.Empty;
        }

        var from = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackFrom)
            ? telemetry.RequestedCaptureBackend
            : telemetry.CaptureFallbackFrom;
        var to = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackTo)
            ? telemetry.CaptureBackend
            : telemetry.CaptureFallbackTo;
        var hresult = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackHresult)
            ? string.Empty
            : $" {telemetry.CaptureFallbackHresult}";

        return $"; fallback {from} -> {to} ({telemetry.CaptureFallbackReason}{hresult})";
    }

    private static string BuildCaptureBackendFailureDetail(RecordingSessionTelemetry telemetry)
    {
        if (string.Equals(telemetry.CaptureFallbackReason, "none", StringComparison.OrdinalIgnoreCase) ||
            !string.IsNullOrWhiteSpace(telemetry.CaptureFallbackTo))
        {
            return string.Empty;
        }

        var backend = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackFrom)
            ? telemetry.CaptureBackend
            : telemetry.CaptureFallbackFrom;
        var hresult = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackHresult)
            ? string.Empty
            : $" {telemetry.CaptureFallbackHresult}";
        return $"; capture backend failure on {backend} ({telemetry.CaptureFallbackReason}{hresult})";
    }
}
