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
                HardwareEncodeStatus: ReadString(root, "hardwareEncodeStatus") ??
                    (ReadBoolean(root, "hardwareEncode") ? "verified-hardware" : "software-or-unverified"),
                EncoderPreference: ReadString(root, "encoderPreference") ?? "auto",
                EncoderSelectionReason: ReadString(root, "encoderSelectionReason") ?? "unknown",
                EncoderFallbackReason: ReadString(root, "encoderFallbackReason") ?? "none",
                EncoderVendor: ReadString(root, "encoderVendor") ?? "unknown",
                EncoderName: ReadString(root, "encoderName") ?? "unknown",
                AdapterLuid: ReadString(root, "adapterLuid") ?? string.Empty,
                VideoCodec: ReadString(root, "videoCodec") ?? "h264",
                EncoderPixelFormat: ReadString(root, "encoderPixelFormat") ?? "unknown",
                GpuPipelineMode: ReadString(root, "gpuPipelineMode") ?? "unknown",
                CaptureSlotCount: ReadInt32(root, "captureSlotCount"),
                CaptureQueueLimit: ReadInt32(root, "captureQueueLimit"),
                IsD3dMultithreadProtected: ReadBoolean(root, "d3dMultithreadProtected"),
                IsGpuHardwareDetected: ReadBoolean(root, "gpuHardwareDetected"),
                IsCpuFallbackAllowed: ReadBoolean(root, "cpuFallbackAllowed"),
                IsCpuFallbackBlocked: ReadBoolean(root, "cpuFallbackBlocked"),
                CpuFallbackBlockReason: ReadString(root, "cpuFallbackBlockReason") ?? "none",
                GpuInitializationHresult: ReadString(root, "gpuInitializationHresult"),
                CopyIntegrityStatus: ReadString(root, "copyIntegrityStatus") ?? "unknown",
                CopyDimensionMismatchCount: ReadInt64(root, "copyDimensionMismatchCount"),
                CopyIntegrityFailureReason: ReadString(root, "copyIntegrityFailureReason"),
                WasWgcStartupAttempted: ReadBoolean(root, "wgcStartupAttempted"),
                WgcFirstFrameLatencyMs: ReadDouble(root, "wgcFirstFrameLatencyMs"),
                AdapterName: ReadString(root, "adapterName"),
                CaptureAttempts: ReadInt64(root, "captureAttemptCount"),
                RequestedFrameRate: ReadInt32(root, "requestedFrameRate"),
                IsMonitorFrameRateMode: ReadBoolean(root, "isMonitorFrameRateMode"),
                MonitorFrameRateLimit: ReadInt32(root, "monitorFrameRateLimit") is var monitorFrameRateLimit && monitorFrameRateLimit > 0
                    ? monitorFrameRateLimit
                    : 120,
                TargetFrameRate: ReadInt32(root, "targetFrameRate"),
                EffectiveFrameRate: ReadDouble(root, "effectiveFrameRate") is var effectiveFrameRate && effectiveFrameRate > 0
                    ? effectiveFrameRate
                    : ReadInt32(root, "targetFrameRate"),
                MonitorRefreshRate: ReadInt32(root, "monitorRefreshRate"),
                WasMonitorFrameRateCapped: ReadBoolean(root, "wasMonitorFrameRateCapped"),
                FrameRatePolicy: ReadString(root, "frameRatePolicy") ?? "unknown",
                FpsCapReason: ReadString(root, "fpsCapReason") ?? "none",
                DuplicatedFrames: ReadInt64(root, "duplicatedFrameCount"),
                AverageFramesPerSecond: ReadDouble(root, "averageEncodedFramesPerSecond"),
                WallDurationMs: ReadDouble(root, "wallDurationMs"),
                RepresentedDurationMs: ReadDouble(root, "representedDurationMs"),
                RepresentedToWallDurationRatio: ReadDouble(root, "representedToWallDurationRatio"),
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
                PeakQueueDepth: ReadInt32(root, "peakQueueDepth"),
                FailureReason: ReadString(root, "failureReason"),
                FailureHresult: ReadString(root, "failureHresult"),
                QualityPresetName: ReadString(root, "qualityPresetName") ?? "Unknown",
                TargetBitrateBps: ReadInt64(root, "targetBitrateBps"),
                MaxBitrateBps: ReadInt64(root, "maxBitrateBps"),
                RateControlMode: ReadString(root, "rateControlMode") ?? "unknown",
                QualityVsSpeed: ReadInt32(root, "qualityVsSpeed"),
                GopSize: ReadInt32(root, "gopSize"),
                IsCabacRequested: ReadBoolean(root, "cabacRequested"),
                EncoderConfigStatus: ReadString(root, "encoderConfigStatus") ?? "unknown",
                ColorPrimaries: ReadString(root, "colorPrimaries") ?? "unknown",
                TransferFunction: ReadString(root, "transferFunction") ?? "unknown",
                YuvMatrix: ReadString(root, "yuvMatrix") ?? "unknown",
                NominalRange: ReadString(root, "nominalRange") ?? "unknown",
                D3dInputColorSpace: ReadString(root, "d3dInputColorSpace") ?? "unknown",
                D3dOutputColorSpace: ReadString(root, "d3dOutputColorSpace") ?? "unknown",
                QualityPolicyVersion: ReadInt32(root, "qualityPolicyVersion"),
                IsEncoderLowLatency: ReadBoolean(root, "encoderLowLatency"),
                IsEncoderRealTime: ReadBoolean(root, "encoderRealTime"),
                IsEncoderFrameDropsAllowed: ReadBoolean(root, "encoderAllowFrameDrops"),
                EncoderFrameRateConversion: ReadString(root, "encoderFrameRateConversion") ?? "unknown",
                VideoProcessorUsage: ReadString(root, "videoProcessorUsage") ?? "unknown",
                IsEdgeEnhancementRequested: ReadBoolean(root, "edgeEnhancementRequested"),
                IsEdgeEnhancementApplied: ReadBoolean(root, "edgeEnhancementApplied"),
                CaptureFrameRate: ReadDouble(root, "captureFrameRate"),
                EncodeContainerFrameRate: ReadInt32(root, "encodeContainerFrameRate"),
                DuplicatedFrameRatio: ReadDouble(root, "duplicatedFrameRatio"),
                H264Level: ReadString(root, "h264Level") ?? "unknown",
                H264LevelValue: ReadInt32(root, "h264LevelValue"),
                CropResizeMismatchReason: ReadString(root, "cropResizeMismatchReason") ?? "none",
                FirstSampleTimestampHns: ReadInt64(root, "firstSampleTimestampHns"),
                LastSampleTimestampHns: ReadInt64(root, "lastSampleTimestampHns"),
                LastSampleDurationHns: ReadInt64(root, "lastSampleDurationHns"),
                AudioMode: ReadString(root, "audioMode") ?? "off",
                AudioCodec: ReadString(root, "audioCodec") ?? "none",
                AudioStatus: ReadString(root, "audioStatus") ?? "off",
                AudioSampleRate: ReadInt32(root, "audioSampleRate"),
                AudioChannels: ReadInt32(root, "audioChannels"),
                SystemAudioDeviceName: ReadString(root, "systemAudioDeviceName") ?? "default",
                MicrophoneDeviceName: ReadString(root, "microphoneDeviceName") ?? "default",
                AudioSamplesWritten: ReadInt64(root, "audioSamplesWritten"),
                AudioPacketsWritten: ReadInt64(root, "audioPacketsWritten"),
                AudioDiscontinuities: ReadInt64(root, "audioDiscontinuities"),
                AudioUnderflows: ReadInt64(root, "audioUnderflows"),
                AudioDriftMs: ReadDouble(root, "audioDriftMs"),
                AudioFailureReason: ReadString(root, "audioFailureReason"));
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
