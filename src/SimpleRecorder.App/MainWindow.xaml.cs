using System.ComponentModel;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using SimpleRecorder.Presentation.ViewModels;
using Windows.Graphics;
using WinRT.Interop;

namespace SimpleRecorder.App;

public sealed partial class MainWindow
{
    private const int WorkbenchWidth = 640;
    private const int WorkbenchHeight = 840;
    private const int OuterMargin = 36;

    private readonly HudViewModel _viewModel;
    private AppWindow? _appWindow;
    private DisplayArea? _displayArea;

    public MainWindow(HudViewModel viewModel)
    {
        _viewModel = viewModel;
        InitializeComponent();
        HudRoot.Bind(_viewModel);
        _viewModel.PropertyChanged += ViewModelOnPropertyChanged;
        Closed += MainWindowOnClosed;

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(DragRegion);
        Title = "SimpleRecorder";

        ConfigureWindow();
    }

    public void BringToFront()
    {
        Activate();
        if (_appWindow is not null)
        {
            _appWindow.IsShownInSwitchers = false;
        }
    }

    private void ViewModelOnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(HudViewModel.IsSettingsOpen))
        {
            ResizeAndReposition();
        }
    }

    private void ConfigureWindow()
    {
        var hwnd = WindowNative.GetWindowHandle(this);
        var windowId = Microsoft.UI.Win32Interop.GetWindowIdFromWindow(hwnd);
        _appWindow = AppWindow.GetFromWindowId(windowId);
        _displayArea = DisplayArea.GetFromWindowId(windowId, DisplayAreaFallback.Primary);
        _appWindow.IsShownInSwitchers = false;
        ResizeAndReposition();

        if (_appWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.SetBorderAndTitleBar(false, false);
            presenter.IsAlwaysOnTop = true;
            presenter.IsMinimizable = false;
            presenter.IsMaximizable = false;
            presenter.IsResizable = false;
        }
    }

    private void ResizeAndReposition()
    {
        if (_appWindow is null)
        {
            return;
        }

        var bounds = _displayArea?.WorkArea ?? new RectInt32(0, 0, WorkbenchWidth + (OuterMargin * 2), WorkbenchHeight + (OuterMargin * 2));
        var maxWidth = Math.Max(360, bounds.Width - (OuterMargin * 2));
        var maxHeight = Math.Max(420, bounds.Height - (OuterMargin * 2));
        var size = new SizeInt32(
            Math.Min(WorkbenchWidth, maxWidth),
            Math.Min(WorkbenchHeight, maxHeight));
        var position = new PointInt32(
            bounds.X + Math.Max(OuterMargin, bounds.Width - size.Width - OuterMargin),
            bounds.Y + Math.Max(OuterMargin, bounds.Height - size.Height - OuterMargin));

        _appWindow.MoveAndResize(new RectInt32(position.X, position.Y, size.Width, size.Height));
    }

    private void MainWindowOnClosed(object sender, WindowEventArgs args)
    {
        Closed -= MainWindowOnClosed;
        _viewModel.PropertyChanged -= ViewModelOnPropertyChanged;
    }
}
