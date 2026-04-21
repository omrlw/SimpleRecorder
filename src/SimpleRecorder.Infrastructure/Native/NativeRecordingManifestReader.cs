using System.Text.Json;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Infrastructure.Native;

internal static class NativeRecordingManifestReader
{
    internal static RecordingSessionTelemetry? TryReadTelemetry(string? recordingSessionPath)
    {
        if (string.IsNullOrWhiteSpace(recordingSessionPath))
        {
            return null;
        }

        var manifestPath = Path.Combine(recordingSessionPath, "manifest.json");
        if (!File.Exists(manifestPath))
        {
            return null;
        }

        try
        {
            using var stream = File.OpenRead(manifestPath);
            using var document = JsonDocument.Parse(stream);
            var root = document.RootElement;

            var backpressureDrops = ReadInt64(root, "backpressureDropCount");
            var captureFailureDrops = ReadInt64(root, "captureFailureCount");
            var droppedFrames = ReadInt64(root, "droppedFrameCount");

            return new RecordingSessionTelemetry(
                RequestedCaptureBackend: ReadString(root, "requestedCaptureBackend") ?? "unknown",
                CaptureBackend: ReadString(root, "captureBackend") ?? "unknown",
                CaptureFallbackFrom: ReadString(root, "captureFallbackFrom"),
                CaptureFallbackTo: ReadString(root, "captureFallbackTo"),
                CaptureFallbackReason: ReadString(root, "captureFallbackReason") ?? "none",
                CaptureFallbackHresult: ReadString(root, "captureFallbackHresult"),
                EncodeBackend: ReadString(root, "encodeBackend") ?? "unknown",
                IsHardwareEncode: ReadBoolean(root, "hardwareEncode"),
                WasWgcStartupAttempted: ReadBoolean(root, "wgcStartupAttempted"),
                WgcFirstFrameLatencyMs: ReadDouble(root, "wgcFirstFrameLatencyMs"),
                AdapterName: ReadString(root, "adapterName"),
                CaptureAttempts: ReadInt64(root, "captureAttemptCount"),
                RequestedFrameRate: ReadInt32(root, "requestedFrameRate"),
                TargetFrameRate: ReadInt32(root, "targetFrameRate"),
                AverageFramesPerSecond: ReadDouble(root, "averageEncodedFramesPerSecond"),
                OutputWidth: ReadInt32(root, "outputWidth"),
                OutputHeight: ReadInt32(root, "outputHeight"),
                CapturedFrames: ReadInt64(root, "capturedFrameCount"),
                EncodedFrames: ReadInt64(root, "encodedFrameCount"),
                DroppedFrames: droppedFrames > 0 ? droppedFrames : backpressureDrops + captureFailureDrops,
                BackpressureDrops: backpressureDrops,
                CaptureFailureDrops: captureFailureDrops,
                PacingOverruns: ReadInt64(root, "pacingOverrunCount"),
                AverageCaptureLatencyMs: ReadDouble(root, "averageCaptureLatencyMs"),
                AverageQueueLatencyMs: ReadDouble(root, "averageQueueLatencyMs"),
                AverageConvertLatencyMs: ReadDouble(root, "averageConvertLatencyMs"),
                AverageEncodeLatencyMs: ReadDouble(root, "averageEncodeLatencyMs"),
                PeakQueueDepth: ReadInt32(root, "peakQueueDepth"));
        }
        catch
        {
            return null;
        }
    }

    private static int ReadInt32(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var property) && property.TryGetInt32(out var value)
            ? value
            : 0;

    private static long ReadInt64(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var property) && property.TryGetInt64(out var value)
            ? value
            : 0;

    private static double ReadDouble(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var property) && property.TryGetDouble(out var value)
            ? value
            : 0;

    private static bool ReadBoolean(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var property) &&
        (property.ValueKind == JsonValueKind.True || property.ValueKind == JsonValueKind.False)
            ? property.GetBoolean()
            : false;

    private static string? ReadString(JsonElement root, string propertyName) =>
        root.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString()
            : null;
}
