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
    private bool _isStarted;
    private bool _isDisposed;

    public AppLifetime(ServiceProvider services)
    {
        _services = services;
        _trayService = services.GetRequiredService<ITrayService>();
        _recorderController = services.GetRequiredService<IRecorderController>();
        _hudViewModel = services.GetRequiredService<HudViewModel>();
    }

    public async Task StartAsync(CancellationToken cancellationToken = default)
    {
        if (_isStarted)
        {
            return;
        }

        _isStarted = true;
        _trayService.CommandInvoked += TrayServiceOnCommandInvoked;
        _recorderController.StatusChanged += (_, snapshot) => _trayService.Update(snapshot);
        _trayService.Initialize();
        cancellationToken.ThrowIfCancellationRequested();
        await _hudViewModel.InitializeAsync();
        _trayService.Update(_recorderController.CurrentStatus);

        _mainWindow = new MainWindow(_hudViewModel);
        _mainWindow.Closed += MainWindowOnClosed;
        _mainWindow.Activate();
    }

    public void Dispose()
    {
        if (_isDisposed)
        {
            return;
        }

        _isDisposed = true;
        if (_mainWindow is not null)
        {
            _mainWindow.Closed -= MainWindowOnClosed;
        }

        _trayService.CommandInvoked -= TrayServiceOnCommandInvoked;
        _trayService.Dispose();
        if (_recorderController is IDisposable disposableRecorder)
        {
            disposableRecorder.Dispose();
        }

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
                await _hudViewModel.ExecutePrimaryActionAsync();
                break;
            case TrayCommand.TogglePause:
                await _hudViewModel.TogglePauseResumeAsync();
                break;
            case TrayCommand.Exit:
                await ShutdownAsync();
                Application.Current.Exit();
                break;
        }
    }

    private async void MainWindowOnClosed(object sender, WindowEventArgs args)
    {
        await ShutdownAsync();
        Application.Current.Exit();
    }

    private async Task ShutdownAsync()
    {
        if (_isDisposed)
        {
            return;
        }

        await _hudViewModel.ShutdownAsync();
        Dispose();
    }
}
