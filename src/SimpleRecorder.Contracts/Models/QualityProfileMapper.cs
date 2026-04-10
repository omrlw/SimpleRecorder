using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public static class QualityProfileMapper
{
    public static RecordingProfile Map(
        FrameRateOption frameRate,
        ResolutionOption resolution,
        QualityPreset preset)
    {
        var baseBitrate = (preset, resolution) switch
        {
            (QualityPreset.SmallFile, ResolutionOption.P720) => 4_000,
            (QualityPreset.SmallFile, _) => 8_000,
            (QualityPreset.Balanced, ResolutionOption.P720) => 6_000,
            (QualityPreset.Balanced, _) => 10_000,
            (QualityPreset.Sharp, ResolutionOption.P720) => 8_000,
            (QualityPreset.Sharp, _) => 14_000,
            _ => 10_000
        };

        if (frameRate == FrameRateOption.Fps60)
        {
            baseBitrate = (int)(baseBitrate * 1.6);
        }
        else if (frameRate == FrameRateOption.Fps24)
        {
            baseBitrate = (int)(baseBitrate * 0.8);
        }

        var encoderProfile = preset == QualityPreset.SmallFile ? "Main" : "High";
        return new RecordingProfile(encoderProfile, baseBitrate, 2, AllowHardwareEncodeFirst: true);
    }
}
