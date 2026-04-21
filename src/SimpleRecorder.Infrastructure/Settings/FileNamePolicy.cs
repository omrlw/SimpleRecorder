using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Infrastructure.Settings;

internal static class FileNamePolicy
{
    internal static string BuildRecordingSessionPath(string saveDirectory, CaptureSourceKind sourceKind)
    {
        Directory.CreateDirectory(saveDirectory);
        var timestamp = DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss");
        return Path.Combine(saveDirectory, $"SimpleRecorder_{timestamp}_{sourceKind}.srrec");
    }

    internal static string BuildRecordingVideoPathFromSessionPath(string recordingSessionPath)
    {
        if (string.IsNullOrWhiteSpace(recordingSessionPath))
        {
            throw new ArgumentException("The recording session path must not be blank.", nameof(recordingSessionPath));
        }

        var sessionPath = Path.GetFullPath(recordingSessionPath);
        var sessionDirectory = Path.GetDirectoryName(sessionPath)
            ?? throw new InvalidOperationException("The recording session path must contain a parent directory.");
        var baseName = Path.GetFileNameWithoutExtension(sessionPath);
        return Path.Combine(sessionDirectory, $"{baseName}.mp4");
    }
}
