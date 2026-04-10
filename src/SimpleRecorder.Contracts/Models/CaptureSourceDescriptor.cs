using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed record CaptureSourceDescriptor(
    CaptureSourceKind Kind,
    string DisplayName,
    string? DisplayId = null,
    string? WindowTitle = null,
    nint WindowHandle = 0,
    ScreenRegion? Region = null,
    string? RememberToken = null)
{
    public static CaptureSourceDescriptor CreateStub(CaptureSourceKind kind) =>
        kind switch
        {
            CaptureSourceKind.Display => new(kind, "Screen 1", "DISPLAY-1", RememberToken: "display:1"),
            CaptureSourceKind.Window => new(kind, "Window", WindowTitle: "Current window", RememberToken: "window:stub"),
            CaptureSourceKind.Region => new(kind, "Region", Region: new ScreenRegion(160, 120, 1280, 720), RememberToken: "region:stub"),
            _ => new(CaptureSourceKind.Display, "Screen 1", "DISPLAY-1", RememberToken: "display:1")
        };
}
