using Microsoft.UI.Xaml.Data;
using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Presentation.Converters;

public sealed class OptionDisplayConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language) =>
        value switch
        {
            FrameRateOption.Monitor => "120 fps",
            FrameRateOption frameRate => $"{(int)frameRate} fps",
            CountdownOption countdown => countdown == CountdownOption.Off ? "Off" : $"{(int)countdown}s",
            ResolutionOption resolution => resolution switch
            {
                ResolutionOption.Auto => "Auto",
                ResolutionOption.P480 => "480p",
                ResolutionOption.P720 => "720p",
                ResolutionOption.P1080 => "1080p",
                ResolutionOption.P1440 => "1440p",
                ResolutionOption.P2160 => "4K",
                _ => resolution.ToString()
            },
            QualityPreset preset => preset switch
            {
                QualityPreset.Quality => "Quality",
                QualityPreset.Balanced => "Balanced",
                QualityPreset.Low => "Low",
                _ => preset.ToString()
            },
            EncoderPreference preference => preference switch
            {
                EncoderPreference.Auto => "Auto",
                EncoderPreference.HardwareOnly => "Hardware",
                EncoderPreference.SoftwareFallback => "Compat",
                _ => preference.ToString()
            },
            VideoCodec codec => codec switch
            {
                VideoCodec.H264 => "H.264",
                VideoCodec.HEVC => "HEVC",
                VideoCodec.AV1 => "AV1",
                _ => codec.ToString()
            },
            _ => value?.ToString() ?? string.Empty
        };

    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
