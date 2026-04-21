using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed record CaptureSourceDescriptor(
    CaptureSourceKind Kind,
    string DisplayName,
    string? DisplayId = null,
    string? WindowTitle = null,
    nint WindowHandle = 0,
    ScreenRegion? Region = null,
    string? RememberToken = null,
    ScreenRegion? Bounds = null)
{
    public static CaptureSourceDescriptor CreateStub(CaptureSourceKind kind) =>
        kind switch
        {
            CaptureSourceKind.Display => new(
                kind,
                "Display 1",
                "DISPLAY-1",
                RememberToken: "display:1",
                Bounds: new ScreenRegion(0, 0, 1920, 1080)),
            CaptureSourceKind.Window => new(
                kind,
                "Window",
                WindowTitle: "Current window",
                RememberToken: "window:stub",
                Bounds: new ScreenRegion(120, 120, 1280, 720)),
            CaptureSourceKind.Region => new(
                kind,
                "Region 1280x720 @ (160, 120)",
                Region: new ScreenRegion(160, 120, 1280, 720),
                RememberToken: "region:160,120,1280,720",
                Bounds: new ScreenRegion(160, 120, 1280, 720)),
            _ => new(
                CaptureSourceKind.Display,
                "Display 1",
                "DISPLAY-1",
                RememberToken: "display:1",
                Bounds: new ScreenRegion(0, 0, 1920, 1080))
        };
}
