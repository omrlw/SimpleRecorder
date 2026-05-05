using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed record RecordingProfile(
    string EncoderProfile,
    int TargetBitrateKbps,
    int KeyFrameIntervalSeconds,
    bool AllowHardwareEncodeFirst,
    VideoCodec Codec = VideoCodec.H264);
