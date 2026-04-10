using System.Collections.ObjectModel;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;
using SimpleRecorder.Presentation.State;
using Windows.UI;

namespace SimpleRecorder.Presentation.ViewModels;

public sealed class HudViewModel : ObservableObject
{
    private static readonly SolidColorBrush DarkBrush = CreateBrush(0x26, 0x26, 0x26);
    private static readonly SolidColorBrush DarkerBrush = CreateBrush(0x16, 0x16, 0x16);
    private static readonly SolidColorBrush LightBrush = CreateBrush(0xE8, 0xE3, 0xDB);
    private static readonly SolidColorBrush TextLightBrush = new(Colors.White);
    private static readonly SolidColorBrush TextDarkBrush = new(Colors.Black);
    private static readonly SolidColorBrush AccentBrush = CreateBrush(0xFF, 0x30, 0x40);
    private static readonly SolidColorBrush SuccessBrush = CreateBrush(0xE3, 0xF5, 0xEC);
    private static readonly SolidColorBrush WarningBrush = CreateBrush(0x38, 0x38, 0x38);

    private readonly DispatcherQueue _dispatcherQueue;
    private readonly IRecorderController _recorderController;
    private readonly ISettingsStore _settingsStore;
    private readonly IAudioDeviceCatalog _audioDeviceCatalog;
    private AppSettings _settings = new();
    private HudSessionState _sessionState = HudSessionState.Idle;
    private HudFeedbackState _feedbackState;
    private CaptureSourceDescriptor _selectedSource = CaptureSourceDescriptor.CreateStub(CaptureSourceKind.Display);
    private bool _isInitialized;
    private bool _isSettingsOpen;
    private int _countdownRemaining;
    private string _statusText = "Ready to record";
    private string _feedbackText = string.Empty;
    private CancellationTokenSource? _countdownCts;

    public HudViewModel(
        IRecorderController recorderController,
        ISettingsStore settingsStore,
        IAudioDeviceCatalog audioDeviceCatalog)
    {
        _dispatcherQueue = DispatcherQueue.GetForCurrentThread();
        _recorderController = recorderController;
        _settingsStore = settingsStore;
        _audioDeviceCatalog = audioDeviceCatalog;

        _recorderController.StatusChanged += RecorderControllerOnStatusChanged;

        Microphones = new ObservableCollection<AudioInputDevice>();
        FrameRateOptions = Enum.GetValues<FrameRateOption>();
        ResolutionOptions = Enum.GetValues<ResolutionOption>();
        QualityOptions = Enum.GetValues<QualityPreset>();
        CountdownOptions = Enum.GetValues<CountdownOption>();

        InitializeCommand = new AsyncRelayCommand(InitializeAsync, () => !_isInitialized);
        PrimaryActionCommand = new AsyncRelayCommand(ExecutePrimaryActionAsync);
        SecondaryActionCommand = new AsyncRelayCommand(ExecuteSecondaryActionAsync);
        SelectDisplaySourceCommand = new RelayCommand(() => SelectSource(CaptureSourceKind.Display));
        SelectWindowSourceCommand = new RelayCommand(() => SelectSource(CaptureSourceKind.Window));
        SelectRegionSourceCommand = new RelayCommand(() => SelectSource(CaptureSourceKind.Region));
        ToggleMicrophoneCommand = new RelayCommand(() => IsMicrophoneEnabled = !IsMicrophoneEnabled);
        ToggleSettingsCommand = new RelayCommand(() => IsSettingsOpen = !IsSettingsOpen);
        CloseSettingsCommand = new RelayCommand(() => IsSettingsOpen = false);
    }

    public ObservableCollection<AudioInputDevice> Microphones { get; }

    public IReadOnlyList<FrameRateOption> FrameRateOptions { get; }

    public IReadOnlyList<ResolutionOption> ResolutionOptions { get; }

    public IReadOnlyList<QualityPreset> QualityOptions { get; }

    public IReadOnlyList<CountdownOption> CountdownOptions { get; }

    public AsyncRelayCommand InitializeCommand { get; }

    public AsyncRelayCommand PrimaryActionCommand { get; }

    public AsyncRelayCommand SecondaryActionCommand { get; }

    public RelayCommand SelectDisplaySourceCommand { get; }

    public RelayCommand SelectWindowSourceCommand { get; }

    public RelayCommand SelectRegionSourceCommand { get; }

    public RelayCommand ToggleMicrophoneCommand { get; }

    public RelayCommand ToggleSettingsCommand { get; }

    public RelayCommand CloseSettingsCommand { get; }

    public HudSessionState SessionState
    {
        get => _sessionState;
        private set
        {
            if (SetProperty(ref _sessionState, value))
            {
                RaiseComputedStateChanged();
            }
        }
    }

    public HudFeedbackState FeedbackState
    {
        get => _feedbackState;
        private set
        {
            if (SetProperty(ref _feedbackState, value))
            {
                OnPropertyChanged(nameof(FeedbackVisibility));
                OnPropertyChanged(nameof(FeedbackBrush));
            }
        }
    }

    public CaptureSourceKind SelectedSourceKind => _selectedSource.Kind;

    public string SelectedSourceLabel => _selectedSource.DisplayName;

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public string FeedbackText
    {
        get => _feedbackText;
        private set => SetProperty(ref _feedbackText, value);
    }

    public bool IsSettingsOpen
    {
        get => _isSettingsOpen;
        set
        {
            if (SetProperty(ref _isSettingsOpen, value))
            {
                OnPropertyChanged(nameof(SettingsVisibility));
            }
        }
    }

    public FrameRateOption SelectedFrameRate
    {
        get => _settings.FrameRate;
        set
        {
            if (_settings.FrameRate == value)
            {
                return;
            }

            _settings.FrameRate = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public ResolutionOption SelectedResolution
    {
        get => _settings.Resolution;
        set
        {
            if (_settings.Resolution == value)
            {
                return;
            }

            _settings.Resolution = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public QualityPreset SelectedQuality
    {
        get => _settings.QualityPreset;
        set
        {
            if (_settings.QualityPreset == value)
            {
                return;
            }

            _settings.QualityPreset = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public CountdownOption SelectedCountdown
    {
        get => _settings.Countdown;
        set
        {
            if (_settings.Countdown == value)
            {
                return;
            }

            _settings.Countdown = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public bool IsSystemAudioEnabled
    {
        get => _settings.SystemAudioEnabled;
        set
        {
            if (_settings.SystemAudioEnabled == value)
            {
                return;
            }

            _settings.SystemAudioEnabled = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public bool IsMicrophoneEnabled
    {
        get => _settings.MicrophoneEnabled;
        set
        {
            if (_settings.MicrophoneEnabled == value)
            {
                return;
            }

            _settings.MicrophoneEnabled = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(MicrophoneButtonBrush));
            OnPropertyChanged(nameof(MicrophoneGlyphForeground));
            PersistSettings();
        }
    }

    public string? SelectedMicrophoneDeviceId
    {
        get => _settings.MicrophoneDeviceId;
        set
        {
            if (string.Equals(_settings.MicrophoneDeviceId, value, StringComparison.Ordinal))
            {
                return;
            }

            _settings.MicrophoneDeviceId = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public string SaveDirectory
    {
        get => _settings.SaveDirectory;
        set
        {
            if (string.Equals(_settings.SaveDirectory, value, StringComparison.Ordinal))
            {
                return;
            }

            _settings.SaveDirectory = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public bool RememberLastSource
    {
        get => _settings.RememberLastSource;
        set
        {
            if (_settings.RememberLastSource == value)
            {
                return;
            }

            _settings.RememberLastSource = value;
            OnPropertyChanged();
            PersistSettings();
        }
    }

    public bool IsDisplaySelected => SelectedSourceKind == CaptureSourceKind.Display;

    public bool IsWindowSelected => SelectedSourceKind == CaptureSourceKind.Window;

    public bool IsRegionSelected => SelectedSourceKind == CaptureSourceKind.Region;

    public bool IsSecondaryActionEnabled => SessionState != HudSessionState.Countdown && SessionState != HudSessionState.StoppingSaving;

    public string SecondaryActionGlyph =>
        SessionState switch
        {
            HudSessionState.Recording => "\uE769",
            HudSessionState.Paused => "\uE768",
            _ => "\uE114"
        };

    public string PrimaryActionGlyph =>
        SessionState switch
        {
            HudSessionState.Countdown => "×",
            HudSessionState.Recording or HudSessionState.Paused => "■",
            _ => "●"
        };

    public string SecondaryActionToolTip =>
        SessionState switch
        {
            HudSessionState.Recording => "Pause recording",
            HudSessionState.Paused => "Resume recording",
            _ => "Take screenshot"
        };

    public Brush PrimaryButtonBrush =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused or HudSessionState.Countdown
            ? AccentBrush
            : DarkerBrush;

    public Brush PrimaryGlyphForeground => TextLightBrush;

    public Brush SecondaryButtonBrush =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused
            ? DarkBrush
            : LightBrush;

    public Brush SecondaryGlyphForeground =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused
            ? TextLightBrush
            : TextDarkBrush;

    public Brush MicrophoneButtonBrush => IsMicrophoneEnabled ? LightBrush : DarkBrush;

    public Brush MicrophoneGlyphForeground => IsMicrophoneEnabled ? TextDarkBrush : TextLightBrush;

    public Visibility FeedbackVisibility => FeedbackState == HudFeedbackState.None ? Visibility.Collapsed : Visibility.Visible;

    public Visibility SettingsVisibility => IsSettingsOpen ? Visibility.Visible : Visibility.Collapsed;

    public Brush FeedbackBrush => FeedbackState == HudFeedbackState.ScreenshotSuccess ? SuccessBrush : WarningBrush;

    public string CountdownLabel => _countdownRemaining > 0 ? _countdownRemaining.ToString() : string.Empty;

    public async Task InitializeAsync()
    {
        if (_isInitialized)
        {
            return;
        }

        _settings = await _settingsStore.LoadAsync().ConfigureAwait(false);
        RaiseSettingsChanged();
        await LoadMicrophonesAsync().ConfigureAwait(false);

        SelectSource(_settings.LastSourceKind);
        await _recorderController.InitializeAsync().ConfigureAwait(false);

        _isInitialized = true;
        InitializeCommand.RaiseCanExecuteChanged();
        StatusText = "HUD ready.";
    }

    public async Task ExecutePrimaryActionAsync()
    {
        switch (SessionState)
        {
            case HudSessionState.Countdown:
                _countdownCts?.Cancel();
                SessionState = HudStateReducer.Reduce(SessionState, HudActionType.CountdownCancelled);
                StatusText = $"{SelectedSourceLabel} is ready.";
                return;
            case HudSessionState.Recording:
            case HudSessionState.Paused:
                SessionState = HudStateReducer.Reduce(SessionState, HudActionType.RequestStop);
                StatusText = "Stopping recording...";
                await _recorderController.StopAsync().ConfigureAwait(false);
                return;
            default:
                await BeginRecordingFlowAsync().ConfigureAwait(false);
                return;
        }
    }

    public async Task ExecuteSecondaryActionAsync()
    {
        if (SessionState is HudSessionState.Recording or HudSessionState.Paused)
        {
            await TogglePauseResumeAsync().ConfigureAwait(false);
            return;
        }

        await CaptureScreenshotAsync().ConfigureAwait(false);
    }

    public async Task TogglePauseResumeAsync()
    {
        if (SessionState == HudSessionState.Recording)
        {
            SessionState = HudStateReducer.Reduce(SessionState, HudActionType.RequestPause);
            await _recorderController.PauseAsync().ConfigureAwait(false);
        }
        else if (SessionState == HudSessionState.Paused)
        {
            SessionState = HudStateReducer.Reduce(SessionState, HudActionType.RequestResume);
            await _recorderController.ResumeAsync().ConfigureAwait(false);
        }
    }

    public async Task CaptureScreenshotAsync()
    {
        var result = await _recorderController
            .CaptureScreenshotAsync(_selectedSource, _settings.ToRecordingOptions())
            .ConfigureAwait(false);

        FeedbackState = result.WasSuccessful ? HudFeedbackState.ScreenshotSuccess : HudFeedbackState.ErrorNonBlocking;
        FeedbackText = result.WasSuccessful
            ? $"Screenshot queued to {Path.GetFileName(result.OutputPath)}"
            : result.ErrorMessage ?? "Screenshot failed.";
        _ = DismissFeedbackAsync();
    }

    private async Task BeginRecordingFlowAsync()
    {
        IsSettingsOpen = false;
        FeedbackState = HudFeedbackState.None;
        FeedbackText = string.Empty;

        if (_settings.Countdown == CountdownOption.Off)
        {
            await _recorderController.StartAsync(_selectedSource, _settings.ToRecordingOptions()).ConfigureAwait(false);
            return;
        }

        SessionState = HudStateReducer.Reduce(SessionState, HudActionType.RequestStart);
        _countdownRemaining = (int)_settings.Countdown;
        RaiseComputedStateChanged();

        _countdownCts?.Cancel();
        _countdownCts = new CancellationTokenSource();

        try
        {
            while (_countdownRemaining > 0)
            {
                StatusText = $"Starting in {_countdownRemaining}s";
                RaiseComputedStateChanged();
                await Task.Delay(TimeSpan.FromSeconds(1), _countdownCts.Token).ConfigureAwait(false);
                _countdownRemaining--;
            }

            SessionState = HudStateReducer.Reduce(SessionState, HudActionType.CountdownCompleted);
            RaiseComputedStateChanged();
            await _recorderController.StartAsync(_selectedSource, _settings.ToRecordingOptions(), _countdownCts.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            _countdownRemaining = 0;
            RaiseComputedStateChanged();
        }
    }

    private void SelectSource(CaptureSourceKind kind)
    {
        _selectedSource = CaptureSourceDescriptor.CreateStub(kind);
        _settings.LastSourceKind = kind;
        _settings.LastSourceToken = _selectedSource.RememberToken;

        if (SessionState is HudSessionState.Idle or HudSessionState.SourceSelected)
        {
            SessionState = HudStateReducer.Reduce(SessionState, HudActionType.SelectSource);
        }

        StatusText = $"{_selectedSource.DisplayName} selected";
        OnPropertyChanged(nameof(SelectedSourceKind));
        OnPropertyChanged(nameof(SelectedSourceLabel));
        OnPropertyChanged(nameof(IsDisplaySelected));
        OnPropertyChanged(nameof(IsWindowSelected));
        OnPropertyChanged(nameof(IsRegionSelected));
        PersistSettings();
    }

    private async Task LoadMicrophonesAsync()
    {
        var devices = await _audioDeviceCatalog.GetInputDevicesAsync().ConfigureAwait(false);

        _dispatcherQueue.TryEnqueue(() =>
        {
            Microphones.Clear();
            foreach (var device in devices)
            {
                Microphones.Add(device);
            }

            if (string.IsNullOrWhiteSpace(_settings.MicrophoneDeviceId))
            {
                SelectedMicrophoneDeviceId = Microphones.FirstOrDefault(device => device.IsDefault)?.Id
                    ?? Microphones.FirstOrDefault()?.Id;
            }
        });
    }

    private void RecorderControllerOnStatusChanged(object? sender, RecorderStatusSnapshot snapshot)
    {
        _dispatcherQueue.TryEnqueue(() =>
        {
            switch (snapshot.State)
            {
                case RecorderState.Recording:
                    SessionState = HudSessionState.Recording;
                    break;
                case RecorderState.Paused:
                    SessionState = HudSessionState.Paused;
                    break;
                case RecorderState.StoppingSaving:
                    SessionState = HudSessionState.StoppingSaving;
                    break;
                case RecorderState.SourceSelected:
                case RecorderState.Idle:
                    SessionState = HudSessionState.SourceSelected;
                    break;
                case RecorderState.ScreenshotSuccess:
                    FeedbackState = HudFeedbackState.ScreenshotSuccess;
                    FeedbackText = snapshot.Message ?? "Screenshot saved.";
                    _ = DismissFeedbackAsync();
                    break;
                case RecorderState.ErrorNonBlocking:
                    FeedbackState = HudFeedbackState.ErrorNonBlocking;
                    FeedbackText = snapshot.Message ?? "Recorder warning.";
                    _ = DismissFeedbackAsync();
                    break;
            }

            StatusText = snapshot.Message ?? StatusText;
            RaiseComputedStateChanged();
        });
    }

    private async Task DismissFeedbackAsync()
    {
        await Task.Delay(1400).ConfigureAwait(false);
        _dispatcherQueue.TryEnqueue(() =>
        {
            FeedbackState = HudFeedbackState.None;
            FeedbackText = string.Empty;
        });
    }

    private void RaiseSettingsChanged()
    {
        OnPropertyChanged(nameof(SelectedFrameRate));
        OnPropertyChanged(nameof(SelectedResolution));
        OnPropertyChanged(nameof(SelectedQuality));
        OnPropertyChanged(nameof(SelectedCountdown));
        OnPropertyChanged(nameof(IsSystemAudioEnabled));
        OnPropertyChanged(nameof(IsMicrophoneEnabled));
        OnPropertyChanged(nameof(SelectedMicrophoneDeviceId));
        OnPropertyChanged(nameof(SaveDirectory));
        OnPropertyChanged(nameof(RememberLastSource));
        OnPropertyChanged(nameof(MicrophoneButtonBrush));
        OnPropertyChanged(nameof(MicrophoneGlyphForeground));
    }

    private void RaiseComputedStateChanged()
    {
        OnPropertyChanged(nameof(IsSecondaryActionEnabled));
        OnPropertyChanged(nameof(SecondaryActionGlyph));
        OnPropertyChanged(nameof(SecondaryActionToolTip));
        OnPropertyChanged(nameof(PrimaryActionGlyph));
        OnPropertyChanged(nameof(PrimaryButtonBrush));
        OnPropertyChanged(nameof(PrimaryGlyphForeground));
        OnPropertyChanged(nameof(SecondaryButtonBrush));
        OnPropertyChanged(nameof(SecondaryGlyphForeground));
        OnPropertyChanged(nameof(CountdownLabel));
        PrimaryActionCommand.RaiseCanExecuteChanged();
        SecondaryActionCommand.RaiseCanExecuteChanged();
    }

    private void PersistSettings()
    {
        _ = _settingsStore.SaveAsync(_settings);
    }

    private static SolidColorBrush CreateBrush(byte r, byte g, byte b, byte a = 255) =>
        new(Color.FromArgb(a, r, g, b));
}
