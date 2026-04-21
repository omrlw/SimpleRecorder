using Microsoft.UI.Xaml;
using SimpleRecorder.App.Bootstrap;

namespace SimpleRecorder.App;

public partial class App : Application
{
    private AppLifetime? _appLifetime;

    public App()
    {
        InitializeComponent();
        UnhandledException += OnUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnCurrentDomainUnhandledException;
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        try
        {
            _appLifetime ??= new AppLifetime(CompositionRoot.BuildServices());
            await _appLifetime.StartAsync();
        }
        catch (Exception ex)
        {
            LogStartupException("OnLaunched", ex);
            throw;
        }
    }

    private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e)
    {
        LogStartupException("Application.UnhandledException", e.Exception);
    }

    private void OnCurrentDomainUnhandledException(object? sender, System.UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception ex)
        {
            LogStartupException("AppDomain.CurrentDomain.UnhandledException", ex);
        }
    }

    private static void LogStartupException(string source, Exception exception)
    {
        try
        {
            var logPath = GetStartupLogPath();
            File.AppendAllText(
                logPath,
                $"[{DateTimeOffset.Now:u}] {source}{Environment.NewLine}{exception}{Environment.NewLine}{Environment.NewLine}");
        }
        catch
        {
            // Startup logging should never add another failure path.
        }
    }

    private static string GetStartupLogPath()
    {
        var logRoot = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "SimpleRecorder",
            "logs");

        Directory.CreateDirectory(logRoot);
        return Path.Combine(logRoot, "startup.log");
    }
}
