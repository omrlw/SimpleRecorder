using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Infrastructure.Settings;
using SimpleRecorder.Infrastructure.SourceSelection;

namespace SimpleRecorder.Infrastructure.Native;

internal sealed class ManagedNativeStubBackend
{
    private const string FallbackNoMediaMessage =
        "Managed fallback is UI-only. No MP4 is produced when the native engine DLL is unavailable.";

    private readonly SemaphoreSlim _gate = new(1, 1);
    private RecorderStatusSnapshot _status = new(RecorderState.Idle, null, Message: "Stub backend ready.");
    private DateTimeOffset? _startedAtUtc;

    public event EventHandler<RecorderStatusSnapshot>? StatusChanged;

    public RecorderStatusSnapshot CurrentStatus => _status;

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        Publish(new RecorderStatusSnapshot(RecorderState.Idle, _status.ActiveSource, Message: "Stub engine initialized."));
        return Task.CompletedTask;
    }

    public async Task StartAsync(
        CaptureSourceDescriptor source,
        RecordingOptions options,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            _startedAtUtc = DateTimeOffset.UtcNow;
            var preview = CaptureSourcePreviewCalculator.Describe(source, options);
            Publish(new RecorderStatusSnapshot(
                RecorderState.Recording,
                source,
                Message: $"Stub fallback active for {source.DisplayName}. Requested bounds {preview.CaptureSizeLabel} @ ({preview.CaptureRegion.X}, {preview.CaptureRegion.Y}). No MP4 will be produced.",
                StartedAtUtc: _startedAtUtc));
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task PauseAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            Publish(_status with { State = RecorderState.Paused, Message = "Stub fallback paused. No MP4 output is being produced." });
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task ResumeAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            Publish(_status with { State = RecorderState.Recording, Message = "Stub fallback resumed. No MP4 output is being produced." });
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task<RecordingResult> StopAsync(RecordingOptions options, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            Publish(_status with { State = RecorderState.StoppingSaving, Message = "Finalizing stub recording..." });
            await Task.Delay(180, cancellationToken).ConfigureAwait(false);

            var duration = _startedAtUtc.HasValue ? DateTimeOffset.UtcNow - _startedAtUtc.Value : TimeSpan.Zero;
            _startedAtUtc = null;

            Publish(new RecorderStatusSnapshot(
                RecorderState.SourceSelected,
                _status.ActiveSource,
                CurrentOutputPath: null,
                Message: FallbackNoMediaMessage,
                StartedAtUtc: null));

            return new RecordingResult(
                false,
                null,
                duration,
                ErrorMessage: FallbackNoMediaMessage);
        }
        finally
        {
            _gate.Release();
        }
    }

    private void Publish(RecorderStatusSnapshot status)
    {
        _status = status;
        StatusChanged?.Invoke(this, status);
    }
}
