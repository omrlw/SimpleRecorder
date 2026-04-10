using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Services;
using SimpleRecorder.Presentation.ViewModels;

namespace SimpleRecorder.App.Bootstrap;

internal sealed class AppLifetime : IDisposable
{
    private readonly ServiceProvider _services;
    private readonly ITrayService _trayService;
    private readonly IRecorderController _recorderController;
    private readonly HudViewModel _hudViewModel;
    private MainWindow? _mainWindow;

    public AppLifetime(ServiceProvider services)
    {
        _services = services;
        _trayService = services.GetRequiredService<ITrayService>();
        _recorderController = services.GetRequiredService<IRecorderController>();
        _hudViewModel = services.GetRequiredService<HudViewModel>();
    }

    public void Start()
    {
        _trayService.CommandInvoked += TrayServiceOnCommandInvoked;
        _recorderController.StatusChanged += (_, snapshot) => _trayService.Update(snapshot);
        _trayService.Initialize();

        _mainWindow = new MainWindow(_hudViewModel);
        _mainWindow.Activate();
        _ = _hudViewModel.InitializeAsync();
    }

    public void Dispose()
    {
        _trayService.CommandInvoked -= TrayServiceOnCommandInvoked;
        _trayService.Dispose();
        _services.Dispose();
    }

    private async void TrayServiceOnCommandInvoked(object? sender, TrayCommand command)
    {
        switch (command)
        {
            case TrayCommand.ShowHud:
                _mainWindow?.BringToFront();
                break;
            case TrayCommand.ToggleRecording:
                await _hudViewModel.ExecutePrimaryActionAsync().ConfigureAwait(false);
                break;
            case TrayCommand.TogglePause:
                await _hudViewModel.TogglePauseResumeAsync().ConfigureAwait(false);
                break;
            case TrayCommand.CaptureScreenshot:
                await _hudViewModel.CaptureScreenshotAsync().ConfigureAwait(false);
                break;
            case TrayCommand.Exit:
                _mainWindow?.Close();
                Application.Current.Exit();
                break;
        }
    }
}
