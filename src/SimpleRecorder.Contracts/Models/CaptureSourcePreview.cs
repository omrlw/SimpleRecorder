namespace SimpleRecorder.Contracts.Models;

public sealed record CaptureSourcePreview(
    ScreenRegion CaptureRegion,
    int OutputWidth,
    int OutputHeight,
    bool WasClippedToVirtualDesktop,
    bool WasScaledFromSource,
    bool WasAdjustedToEvenDimensions)
{
    public string CaptureSizeLabel => $"{CaptureRegion.Width}x{CaptureRegion.Height}";

    public string OutputSizeLabel => $"{OutputWidth}x{OutputHeight}";
}
