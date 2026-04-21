using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Contracts.Services;

public interface ICaptureSourcePicker
{
    Task<CaptureSourceDescriptor?> SelectDisplaySourceAsync(
        CaptureSourceDescriptor? currentSource,
        CancellationToken cancellationToken = default);

    Task<CaptureSourceDescriptor?> SelectWindowSourceAsync(
        CaptureSourceDescriptor? currentSource,
        CancellationToken cancellationToken = default);

    Task<CaptureSourceDescriptor?> SelectRegionSourceAsync(
        CaptureSourceDescriptor? currentSource,
        CancellationToken cancellationToken = default);

    CaptureSourceDescriptor ResolveInitialSource(CaptureSourceKind preferredKind, string? rememberToken);

    CaptureSourcePreview Describe(CaptureSourceDescriptor source, RecordingOptions options);
}
