using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Infrastructure.Settings;

internal static class FileNamePolicy
{
    internal static string BuildVideoPath(string saveDirectory, CaptureSourceKind sourceKind)
    {
        Directory.CreateDirectory(saveDirectory);
        var timestamp = DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss");
        return Path.Combine(saveDirectory, $"SimpleRecorder_{timestamp}_{sourceKind}.mp4");
    }

    internal static string BuildScreenshotPath(string saveDirectory)
    {
        Directory.CreateDirectory(saveDirectory);
        var timestamp = DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss");
        return Path.Combine(saveDirectory, $"SimpleRecorderShot_{timestamp}.png");
    }
}
