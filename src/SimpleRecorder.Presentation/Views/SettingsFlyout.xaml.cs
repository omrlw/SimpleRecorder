using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using SimpleRecorder.Presentation.Converters;
using SimpleRecorder.Presentation.ViewModels;

namespace SimpleRecorder.Presentation.Views;

public sealed partial class SettingsFlyout
{
    private readonly OptionDisplayConverter _optionDisplayConverter = new();

    public SettingsFlyout()
    {
        InitializeComponent();
    }

    private HudViewModel? ViewModel => DataContext as HudViewModel;

    private void OnMicrophoneFlyoutOpening(object sender, object e)
    {
        if (sender is not MenuFlyout flyout || ViewModel is null)
        {
            return;
        }

        flyout.Items.Clear();
        foreach (var device in ViewModel.Microphones)
        {
            var item = new ToggleMenuFlyoutItem
            {
                Text = device.DisplayName,
                IsChecked = string.Equals(device.Id, ViewModel.SelectedMicrophoneDeviceId, StringComparison.Ordinal)
            };

            ApplyMenuItemStyle(item);
            var deviceId = device.Id;
            item.Click += (_, _) => ViewModel.SelectedMicrophoneDeviceId = deviceId;
            flyout.Items.Add(item);
        }
    }

    private void OnFrameRateFlyoutOpening(object sender, object e)
    {
        if (sender is not MenuFlyout flyout || ViewModel is null)
        {
            return;
        }

        flyout.Items.Clear();
        foreach (var option in ViewModel.FrameRateOptions)
        {
            var item = new ToggleMenuFlyoutItem
            {
                Text = FormatOption(option),
                IsChecked = option == ViewModel.SelectedFrameRate
            };

            ApplyMenuItemStyle(item);
            var selectedOption = option;
            item.Click += (_, _) => ViewModel.SelectedFrameRate = selectedOption;
            flyout.Items.Add(item);
        }
    }

    private void OnCountdownFlyoutOpening(object sender, object e)
    {
        if (sender is not MenuFlyout flyout || ViewModel is null)
        {
            return;
        }

        flyout.Items.Clear();
        foreach (var option in ViewModel.CountdownOptions)
        {
            var item = new ToggleMenuFlyoutItem
            {
                Text = FormatOption(option),
                IsChecked = option == ViewModel.SelectedCountdown
            };

            ApplyMenuItemStyle(item);
            var selectedOption = option;
            item.Click += (_, _) => ViewModel.SelectedCountdown = selectedOption;
            flyout.Items.Add(item);
        }
    }

    private void OnEncoderPreferenceFlyoutOpening(object sender, object e)
    {
        if (sender is not MenuFlyout flyout || ViewModel is null)
        {
            return;
        }

        flyout.Items.Clear();
        foreach (var option in ViewModel.EncoderPreferenceOptions)
        {
            var item = new ToggleMenuFlyoutItem
            {
                Text = FormatOption(option),
                IsChecked = option == ViewModel.SelectedEncoderPreference
            };

            ApplyMenuItemStyle(item);
            var selectedOption = option;
            item.Click += (_, _) => ViewModel.SelectedEncoderPreference = selectedOption;
            flyout.Items.Add(item);
        }
    }

    private string FormatOption(object value) =>
        _optionDisplayConverter.Convert(value, typeof(string), string.Empty, string.Empty)?.ToString() ?? string.Empty;

    private static void ApplyMenuItemStyle(ToggleMenuFlyoutItem item)
    {
        if (Application.Current.Resources.TryGetValue("HudToggleMenuFlyoutItemStyle", out var style) && style is Style typedStyle)
        {
            item.Style = typedStyle;
        }
    }
}
