using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Infrastructure.Native;

internal static class NativeStructMapper
{
    internal static CaptureSourceDescriptor NormalizeSource(CaptureSourceDescriptor source)
    {
        var region = source.Region ?? source.Bounds;
        if (source.Kind == CaptureSourceKind.Region)
        {
            return source with
            {
                DisplayName = string.IsNullOrWhiteSpace(source.DisplayName)
                    ? BuildRegionDisplayName(region)
                    : source.DisplayName,
                Region = region,
                Bounds = region
            };
        }

        return source with
        {
            DisplayName = string.IsNullOrWhiteSpace(source.DisplayName) ? source.Kind.ToString() : source.DisplayName,
            Bounds = source.Bounds ?? source.Region
        };
    }

    internal static RecordingOptions NormalizeOptions(RecordingOptions options) =>
        options with
        {
            SaveDirectory = string.IsNullOrWhiteSpace(options.SaveDirectory)
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyVideos), "SimpleRecorder")
                : options.SaveDirectory
        };

    internal static NativeMethods.SrCaptureSource ToNativeSource(CaptureSourceDescriptor source)
    {
        var sourceRegion = ResolveBoundsForNative(source);
        var region = sourceRegion is null
            ? new NativeMethods.SrRect(0, 0, 0, 0)
            : new NativeMethods.SrRect(sourceRegion.X, sourceRegion.Y, sourceRegion.Width, sourceRegion.Height);

        return new NativeMethods.SrCaptureSource(
            NativeMethods.CurrentStructVersion,
            (int)source.Kind,
            source.WindowHandle,
            region);
    }

    internal static NativeMethods.SrRecordingOptions ToNativeOptions(RecordingOptions options) =>
        new(
            NativeMethods.CurrentStructVersion,
            (int)options.FrameRate,
            (int)options.Resolution,
            (int)options.QualityPreset,
            (int)options.Countdown,
            options.IsSystemAudioEnabled ? 1 : 0,
            options.IsMicrophoneEnabled ? 1 : 0,
            (int)options.EncoderPreference,
            (int)options.VideoCodec);

    internal static RecorderState ToContractState(NativeMethods.SrRecorderState state) =>
        state switch
        {
            NativeMethods.SrRecorderState.Idle => RecorderState.Idle,
            NativeMethods.SrRecorderState.SourceSelected => RecorderState.SourceSelected,
            NativeMethods.SrRecorderState.Countdown => RecorderState.Countdown,
            NativeMethods.SrRecorderState.Recording => RecorderState.Recording,
            NativeMethods.SrRecorderState.Paused => RecorderState.Paused,
            NativeMethods.SrRecorderState.StoppingSaving => RecorderState.StoppingSaving,
            _ => RecorderState.ErrorNonBlocking
        };

    internal static string BuildMessage(RecorderState state, CaptureSourceDescriptor? activeSource) =>
        state switch
        {
            RecorderState.Idle => "Native engine initialized.",
            RecorderState.SourceSelected => $"{(activeSource?.DisplayName ?? "Source")} is ready.",
            RecorderState.Countdown => "Countdown in progress.",
            RecorderState.Recording => $"Recording {(activeSource?.DisplayName ?? "source")} with the live MP4 pipeline.",
            RecorderState.Paused => "Recording paused.",
            RecorderState.StoppingSaving => "Finalizing MP4 output...",
            _ => "Native engine reported a non-blocking warning."
        };

    internal static CaptureSourceDescriptor CreateSourceStub(CaptureSourceKind kind) =>
        NormalizeSource(CaptureSourceDescriptor.CreateStub(kind));

    private static ScreenRegion? ResolveBoundsForNative(CaptureSourceDescriptor source)
    {
        var region = source.Region ?? source.Bounds;
        if (region is null)
        {
            return null;
        }

        if (!region.MeetsMinimumSize)
        {
            throw new InvalidOperationException(
                $"Capture requires bounds of at least {ScreenRegion.MinimumDimension}x{ScreenRegion.MinimumDimension} physical pixels.");
        }

        return region;
    }

    private static string BuildRegionDisplayName(ScreenRegion? region) =>
        region is null
            ? "Region"
            : $"Region {region.Width}x{region.Height}";
}
