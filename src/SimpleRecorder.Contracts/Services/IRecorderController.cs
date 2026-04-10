using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Contracts.Services;

public interface IRecorderController
{
    event EventHandler<RecorderStatusSnapshot>? StatusChanged;

    RecorderStatusSnapshot CurrentStatus { get; }

    Task InitializeAsync(CancellationToken cancellationToken = default);

    Task StartAsync(CaptureSourceDescriptor source, RecordingOptions options, CancellationToken cancellationToken = default);

    Task PauseAsync(CancellationToken cancellationToken = default);

    Task ResumeAsync(CancellationToken cancellationToken = default);

    Task<RecordingResult> StopAsync(CancellationToken cancellationToken = default);

    Task<ScreenshotResult> CaptureScreenshotAsync(
        CaptureSourceDescriptor source,
        RecordingOptions options,
        CancellationToken cancellationToken = default);
}
