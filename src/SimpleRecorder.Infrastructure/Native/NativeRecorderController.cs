using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;

namespace SimpleRecorder.Infrastructure.Native;

public sealed class NativeRecorderController : IRecorderController
{
    private readonly ManagedNativeStubBackend _stubBackend = new();
    private bool _isNativeLibraryAvailable;
    private nint _nativeHandle;

    public NativeRecorderController()
    {
        _stubBackend.StatusChanged += (_, snapshot) =>
        {
            CurrentStatus = snapshot;
            StatusChanged?.Invoke(this, snapshot);
        };
    }

    public event EventHandler<RecorderStatusSnapshot>? StatusChanged;

    public RecorderStatusSnapshot CurrentStatus { get; private set; } =
        new(RecorderState.Idle, null, Message: "Recorder not initialized.");

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        _isNativeLibraryAvailable = NativeMethods.TryLoad(out _nativeHandle);
        if (_isNativeLibraryAvailable)
        {
            NativeMethods.FreeIfLoaded(_nativeHandle);
            _nativeHandle = nint.Zero;
        }

        return _stubBackend.InitializeAsync(cancellationToken);
    }

    public Task StartAsync(
        CaptureSourceDescriptor source,
        RecordingOptions options,
        CancellationToken cancellationToken = default) =>
        _stubBackend.StartAsync(
            NativeStructMapper.NormalizeSource(source),
            NativeStructMapper.NormalizeOptions(options),
            cancellationToken);

    public Task PauseAsync(CancellationToken cancellationToken = default) =>
        _stubBackend.PauseAsync(cancellationToken);

    public Task ResumeAsync(CancellationToken cancellationToken = default) =>
        _stubBackend.ResumeAsync(cancellationToken);

    public Task<RecordingResult> StopAsync(CancellationToken cancellationToken = default)
    {
        var saveDirectory = CurrentStatus.CurrentOutputPath is { Length: > 0 }
            ? Path.GetDirectoryName(CurrentStatus.CurrentOutputPath!)!
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyVideos), "SimpleRecorder");

        var fallbackOptions = new AppSettings { SaveDirectory = saveDirectory }.ToRecordingOptions();
        return _stubBackend.StopAsync(fallbackOptions, cancellationToken);
    }

    public Task<ScreenshotResult> CaptureScreenshotAsync(
        CaptureSourceDescriptor source,
        RecordingOptions options,
        CancellationToken cancellationToken = default) =>
        _stubBackend.CaptureScreenshotAsync(source, NativeStructMapper.NormalizeOptions(options), cancellationToken);
}
