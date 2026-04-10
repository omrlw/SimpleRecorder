using Microsoft.UI.Xaml;
using SimpleRecorder.App.Bootstrap;

namespace SimpleRecorder.App;

public partial class App : Application
{
    private AppLifetime? _appLifetime;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _appLifetime ??= new AppLifetime(CompositionRoot.BuildServices());
        _appLifetime.Start();
    }
}
