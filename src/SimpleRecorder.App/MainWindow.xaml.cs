using System.ComponentModel;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using SimpleRecorder.Presentation.ViewModels;
using Windows.Graphics;
using WinRT.Interop;

namespace SimpleRecorder.App;

public sealed partial class MainWindow
{
    private readonly HudViewModel _viewModel;
    private AppWindow? _appWindow;

    public MainWindow(HudViewModel viewModel)
    {
        _viewModel = viewModel;
        InitializeComponent();
        HudRoot.Bind(_viewModel);
        _viewModel.PropertyChanged += ViewModelOnPropertyChanged;

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(DragRegion);
        Title = "SimpleRecorder";

        ConfigureWindow();
    }

    public void BringToFront()
    {
        Activate();
    }

    private void ViewModelOnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(HudViewModel.IsSettingsOpen))
        {
            ResizeForSettings();
        }
    }

    private void ConfigureWindow()
    {
        var hwnd = WindowNative.GetWindowHandle(this);
        var windowId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(hwnd);
        _appWindow = AppWindow.GetFromWindowId(windowId);
        ResizeForSettings();

        if (_appWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.IsAlwaysOnTop = true;
            presenter.IsMinimizable = false;
            presenter.IsMaximizable = false;
            presenter.IsResizable = false;
        }
    }

    private void ResizeForSettings()
    {
        _appWindow?.Resize(_viewModel.IsSettingsOpen ? new SizeInt32(596, 520) : new SizeInt32(596, 260));
    }
}
