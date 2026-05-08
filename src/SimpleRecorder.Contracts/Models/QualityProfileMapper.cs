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
            (QualityPreset.Low, ResolutionOption.P480) => 3_500,
            (QualityPreset.Low, ResolutionOption.P720) => 5_000,
            (QualityPreset.Low, ResolutionOption.P1080) => 9_000,
            (QualityPreset.Low, ResolutionOption.P1440) => 16_000,
            (QualityPreset.Low, ResolutionOption.P2160) => 28_000,
            (QualityPreset.Balanced, ResolutionOption.P480) => 6_000,
            (QualityPreset.Balanced, ResolutionOption.P720) => 10_000,
            (QualityPreset.Balanced, ResolutionOption.P1080) => 18_000,
            (QualityPreset.Balanced, ResolutionOption.P1440) => 30_000,
            (QualityPreset.Balanced, ResolutionOption.P2160) => 52_000,
            (QualityPreset.Quality, ResolutionOption.P480) => 7_500,
            (QualityPreset.Quality, ResolutionOption.P720) => 12_000,
            (QualityPreset.Quality, ResolutionOption.P1080) => 22_000,
            (QualityPreset.Quality, ResolutionOption.P1440) => 38_000,
            (QualityPreset.Quality, ResolutionOption.P2160) => 68_000,
            _ => 16_000
        };

        if (frameRate == FrameRateOption.Monitor)
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

        var encoderProfile = preset == QualityPreset.Low ? "Main" : "High";
        return new RecordingProfile(encoderProfile, baseBitrate, 2, AllowHardwareEncodeFirst: true, VideoCodec.H264);
    }
}
