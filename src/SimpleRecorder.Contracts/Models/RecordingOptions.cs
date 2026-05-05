using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed record RecordingOptions(
    FrameRateOption FrameRate,
    ResolutionOption Resolution,
    QualityPreset QualityPreset,
    CountdownOption Countdown,
    bool IsSystemAudioEnabled,
    bool IsMicrophoneEnabled,
    string? MicrophoneDeviceId,
    string SaveDirectory,
    EncoderPreference EncoderPreference,
    VideoCodec VideoCodec,
    RecordingProfile VideoProfile)
{
    public bool ShouldShowCountdown => Countdown != CountdownOption.Off;
}
