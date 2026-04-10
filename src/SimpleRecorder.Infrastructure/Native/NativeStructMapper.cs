using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Infrastructure.Native;

internal static class NativeStructMapper
{
    internal static CaptureSourceDescriptor NormalizeSource(CaptureSourceDescriptor source) =>
        source with
        {
            DisplayName = string.IsNullOrWhiteSpace(source.DisplayName) ? source.Kind.ToString() : source.DisplayName
        };

    internal static RecordingOptions NormalizeOptions(RecordingOptions options) =>
        options with
        {
            SaveDirectory = string.IsNullOrWhiteSpace(options.SaveDirectory)
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyVideos), "SimpleRecorder")
                : options.SaveDirectory
        };

    internal static CaptureSourceDescriptor CreateSourceStub(CaptureSourceKind kind) =>
        NormalizeSource(CaptureSourceDescriptor.CreateStub(kind));
}
