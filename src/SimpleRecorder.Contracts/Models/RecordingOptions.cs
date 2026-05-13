using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed record RecordingOptions(
    FrameRateOption FrameRate,
    ResolutionOption Resolution,
    QualityPreset QualityPreset,
    CountdownOption Countdown,
    AudioCaptureMode AudioMode,
    string? MicrophoneDeviceId,
    string SaveDirectory,
    EncoderPreference EncoderPreference,
    VideoCodec VideoCodec,
    RecordingProfile VideoProfile)
{
    public bool ShouldShowCountdown => Countdown != CountdownOption.Off;

    public bool IsSystemAudioEnabled =>
        AudioMode is AudioCaptureMode.System or AudioCaptureMode.SystemAndMicrophone;

    public bool IsMicrophoneEnabled =>
        AudioMode is AudioCaptureMode.Microphone or AudioCaptureMode.SystemAndMicrophone;
}
