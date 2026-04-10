using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Infrastructure.Settings;

namespace SimpleRecorder.Infrastructure.Native;

internal sealed class ManagedNativeStubBackend
{
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
            Publish(new RecorderStatusSnapshot(
                RecorderState.Recording,
                source,
                Message: $"Recording {source.DisplayName} with {options.VideoProfile.TargetBitrateKbps / 1000.0:F1} Mbps target.",
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
            Publish(_status with { State = RecorderState.Paused, Message = "Recording paused." });
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
            Publish(_status with { State = RecorderState.Recording, Message = "Recording resumed." });
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

            var outputPath = FileNamePolicy.BuildVideoPath(options.SaveDirectory, _status.ActiveSource?.Kind ?? CaptureSourceKind.Display);
            var duration = _startedAtUtc.HasValue ? DateTimeOffset.UtcNow - _startedAtUtc.Value : TimeSpan.Zero;

            Publish(new RecorderStatusSnapshot(
                RecorderState.SourceSelected,
                _status.ActiveSource,
                CurrentOutputPath: outputPath,
                Message: "Stub recording stopped.",
                StartedAtUtc: null));

            return new RecordingResult(true, outputPath, duration);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task<ScreenshotResult> CaptureScreenshotAsync(
        CaptureSourceDescriptor source,
        RecordingOptions options,
        CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var outputPath = FileNamePolicy.BuildScreenshotPath(options.SaveDirectory);
            Publish(new RecorderStatusSnapshot(
                RecorderState.ScreenshotSuccess,
                source,
                CurrentOutputPath: outputPath,
                Message: "Stub screenshot captured."));

            return new ScreenshotResult(true, outputPath);
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
