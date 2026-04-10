using Microsoft.UI.Xaml;
using SimpleRecorder.Presentation.ViewModels;
using Windows.UI.ViewManagement;

namespace SimpleRecorder.Presentation.Views;

public sealed partial class HudView
{
    public HudView()
    {
        InitializeComponent();
        ApplyReducedMotionPreference();
    }

    public void Bind(HudViewModel viewModel)
    {
        DataContext = viewModel;
    }

    private void ApplyReducedMotionPreference()
    {
        var uiSettings = new UISettings();
        if (!uiSettings.AnimationsEnabled)
        {
            ContentPanel.Transitions = null;
        }
    }
}
