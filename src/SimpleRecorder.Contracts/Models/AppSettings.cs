using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed class AppSettings
{
    public FrameRateOption FrameRate { get; set; } = FrameRateOption.Fps60;

    public ResolutionOption Resolution { get; set; } = ResolutionOption.Auto;

    public QualityPreset QualityPreset { get; set; } = QualityPreset.Balanced;

    public CountdownOption Countdown { get; set; } = CountdownOption.FiveSeconds;

    public AudioCaptureMode AudioMode { get; set; } = AudioCaptureMode.Off;

    public bool SystemAudioEnabled { get; set; } = false;

    public bool MicrophoneEnabled { get; set; } = false;

    public string? MicrophoneDeviceId { get; set; }

    public string SaveDirectory { get; set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.MyVideos),
        "SimpleRecorder");

    public EncoderPreference EncoderPreference { get; set; } = EncoderPreference.Auto;

    public VideoCodec VideoCodec { get; set; } = VideoCodec.H264;

    public bool RememberLastSource { get; set; } = true;

    public CaptureSourceKind LastSourceKind { get; set; } = CaptureSourceKind.Display;

    public string? LastSourceToken { get; set; } = "display:1";

    public RecordingOptions ToRecordingOptions() =>
        new(
            FrameRate,
            Resolution,
            QualityPreset,
            Countdown,
            ResolveAudioMode(),
            MicrophoneDeviceId,
            SaveDirectory,
            EncoderPreference,
            VideoCodec,
            QualityProfileMapper.Map(FrameRate, Resolution, QualityPreset));

    public AudioCaptureMode ResolveAudioMode()
    {
        if (AudioMode != AudioCaptureMode.Off)
        {
            return AudioMode;
        }

        return (SystemAudioEnabled, MicrophoneEnabled) switch
        {
            (true, true) => AudioCaptureMode.SystemAndMicrophone,
            (true, false) => AudioCaptureMode.System,
            (false, true) => AudioCaptureMode.Microphone,
            _ => AudioCaptureMode.Off
        };
    }
}
