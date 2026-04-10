namespace SimpleRecorder.Contracts.Models;

public sealed record RecordingProfile(
    string EncoderProfile,
    int TargetBitrateKbps,
    int KeyFrameIntervalSeconds,
    bool AllowHardwareEncodeFirst);
