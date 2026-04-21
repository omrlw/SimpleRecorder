using Microsoft.UI.Xaml.Data;
using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Presentation.Converters;

public sealed class OptionDisplayConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language) =>
        value switch
        {
            FrameRateOption frameRate => $"{(int)frameRate}fps",
            CountdownOption countdown => countdown == CountdownOption.Off ? "Off" : $"{(int)countdown}s",
            ResolutionOption resolution => resolution switch
            {
                ResolutionOption.Auto => "Auto",
                ResolutionOption.P720 => "720p",
                ResolutionOption.P1080 => "1080p",
                _ => resolution.ToString()
            },
            QualityPreset preset => preset switch
            {
                QualityPreset.Balanced => "Balanced",
                QualityPreset.Sharp => "Sharp",
                QualityPreset.SmallFile => "Small file",
                _ => preset.ToString()
            },
            _ => value?.ToString() ?? string.Empty
        };

    public object ConvertBack(object value, Type targetType, object parameter, string language) =>
        throw new NotSupportedException();
}
