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
            (QualityPreset.SmallFile, ResolutionOption.P720) => 5_000,
            (QualityPreset.SmallFile, _) => 9_000,
            (QualityPreset.Balanced, ResolutionOption.P720) => 9_000,
            (QualityPreset.Balanced, _) => 16_000,
            (QualityPreset.Sharp, ResolutionOption.P720) => 12_000,
            (QualityPreset.Sharp, _) => 22_000,
            _ => 16_000
        };

        if (frameRate == FrameRateOption.Fps120)
        {
            baseBitrate = (int)(baseBitrate * 2.1);
        }
        else if (frameRate == FrameRateOption.Fps60)
        {
            baseBitrate = (int)(baseBitrate * 1.6);
        }
        else if (frameRate == FrameRateOption.Fps24)
        {
            baseBitrate = (int)(baseBitrate * 0.8);
        }

        var encoderProfile = preset == QualityPreset.SmallFile ? "Main" : "High";
        return new RecordingProfile(encoderProfile, baseBitrate, 2, AllowHardwareEncodeFirst: true, VideoCodec.H264);
    }
}
