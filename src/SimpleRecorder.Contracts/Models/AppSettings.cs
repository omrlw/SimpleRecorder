using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed class AppSettings
{
    public FrameRateOption FrameRate { get; set; } = FrameRateOption.Fps60;

    public ResolutionOption Resolution { get; set; } = ResolutionOption.Auto;

    public QualityPreset QualityPreset { get; set; } = QualityPreset.Balanced;

    public CountdownOption Countdown { get; set; } = CountdownOption.Off;

    public bool SystemAudioEnabled { get; set; } = true;

    public bool MicrophoneEnabled { get; set; }

    public string? MicrophoneDeviceId { get; set; }

    public string SaveDirectory { get; set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.MyVideos),
        "SimpleRecorder");

    public bool RememberLastSource { get; set; } = true;

    public CaptureSourceKind LastSourceKind { get; set; } = CaptureSourceKind.Display;

    public string? LastSourceToken { get; set; } = "display:1";

    public RecordingOptions ToRecordingOptions() =>
        new(
            FrameRate,
            Resolution,
            QualityPreset,
            Countdown,
            SystemAudioEnabled,
            MicrophoneEnabled,
            MicrophoneDeviceId,
            SaveDirectory,
            QualityProfileMapper.Map(FrameRate, Resolution, QualityPreset));
}
