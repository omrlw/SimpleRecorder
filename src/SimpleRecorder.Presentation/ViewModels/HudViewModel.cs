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
    private const string DisplaySourceButtonLabelText = "Display";
    private const string WindowSourceButtonLabelText = "Window";
    private const string RegionSourceButtonLabelText = "Region";

    private static readonly TimeSpan FeedbackDisplayDuration = TimeSpan.FromMilliseconds(1200);
    private static readonly SolidColorBrush DarkBrush = CreateBrush(0x26, 0x26, 0x26);
    private static readonly SolidColorBrush DarkerBrush = CreateBrush(0x00, 0x00, 0x00);
    private static readonly SolidColorBrush TransparentBrush = CreateBrush(0x00, 0x00, 0x00, 0x00);
    private static readonly SolidColorBrush SelectedSourceBrush = CreateBrush(0x11, 0x11, 0x11);
    private static readonly SolidColorBrush LightBrush = CreateBrush(0xD9, 0xD9, 0xD9);
    private static readonly SolidColorBrush TextLightBrush = CreateBrush(0xF4, 0xF4, 0xF4);
    private static readonly SolidColorBrush TextDarkBrush = CreateBrush(0x17, 0x17, 0x17);
    private static readonly SolidColorBrush AccentBrush = CreateBrush(0xFB, 0x2C, 0x36);
    private static readonly SolidColorBrush SelectedSourceStrokeBrush = CreateBrush(0xFB, 0x2C, 0x36, 0x59);
    private static readonly SolidColorBrush WarningBrush = CreateBrush(0x38, 0x38, 0x38);
    private static readonly SolidColorBrush IndicatorOnBrush = CreateBrush(0x46, 0xD4, 0x78);

    private readonly DispatcherQueue _dispatcherQueue;
    private readonly IRecorderController _recorderController;
    private readonly ISettingsStore _settingsStore;
    private readonly IAudioDeviceCatalog _audioDeviceCatalog;
    private readonly ICaptureSourcePicker _captureSourcePicker;
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
    private RecorderStatusSnapshot _lastRecorderStatus;

    public HudViewModel(
        IRecorderController recorderController,
        ISettingsStore settingsStore,
        IAudioDeviceCatalog audioDeviceCatalog,
        ICaptureSourcePicker captureSourcePicker)
    {
        _dispatcherQueue = DispatcherQueue.GetForCurrentThread();
        _recorderController = recorderController;
        _settingsStore = settingsStore;
        _audioDeviceCatalog = audioDeviceCatalog;
        _captureSourcePicker = captureSourcePicker;
        _lastRecorderStatus = recorderController.CurrentStatus;

        _recorderController.StatusChanged += RecorderControllerOnStatusChanged;

        Microphones = new ObservableCollection<AudioInputDevice>();
        FrameRateOptions =
        [
            FrameRateOption.Fps24,
            FrameRateOption.Fps30,
            FrameRateOption.Fps60,
            FrameRateOption.Monitor
        ];
        ResolutionOptions = BuildResolutionOptions();
        QualityOptions =
        [
            QualityPreset.Quality,
            QualityPreset.Balanced,
            QualityPreset.Low
        ];
        CountdownOptions = Enum.GetValues<CountdownOption>();
        EncoderPreferenceOptions = Enum.GetValues<EncoderPreference>();

        InitializeCommand = new AsyncRelayCommand(InitializeAsync, () => !_isInitialized);
        PrimaryActionCommand = new AsyncRelayCommand(ExecutePrimaryActionAsync);
        SecondaryActionCommand = new AsyncRelayCommand(ExecuteSecondaryActionAsync);
        SelectDisplaySourceCommand = new AsyncRelayCommand(SelectDisplaySourceAsync, () => IsSourceSelectionEnabled);
        SelectWindowSourceCommand = new AsyncRelayCommand(SelectWindowSourceAsync, () => IsSourceSelectionEnabled);
        SelectRegionSourceCommand = new AsyncRelayCommand(SelectRegionSourceAsync, () => IsSourceSelectionEnabled);
        ToggleMicrophoneCommand = new RelayCommand(() =>
        {
            if (IsMicrophoneToggleEnabled)
            {
                IsMicrophoneEnabled = !IsMicrophoneEnabled;
            }
        });
        ToggleSettingsCommand = new RelayCommand(() =>
        {
            if (IsSettingsEnabled)
            {
                IsSettingsOpen = !IsSettingsOpen;
            }
        });
        CloseSettingsCommand = new RelayCommand(() => IsSettingsOpen = false);
    }

    public ObservableCollection<AudioInputDevice> Microphones { get; }

    public IReadOnlyList<FrameRateOption> FrameRateOptions { get; }

    public IReadOnlyList<ResolutionOptionItem> ResolutionOptions { get; private set; }

    public IReadOnlyList<QualityPreset> QualityOptions { get; }

    public IReadOnlyList<CountdownOption> CountdownOptions { get; }

    public IReadOnlyList<EncoderPreference> EncoderPreferenceOptions { get; }

    public AsyncRelayCommand InitializeCommand { get; }

    public AsyncRelayCommand PrimaryActionCommand { get; }

    public AsyncRelayCommand SecondaryActionCommand { get; }

    public AsyncRelayCommand SelectDisplaySourceCommand { get; }

    public AsyncRelayCommand SelectWindowSourceCommand { get; }

    public AsyncRelayCommand SelectRegionSourceCommand { get; }

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
                OnPropertyChanged(nameof(FeedbackForegroundBrush));
            }
        }
    }

    public CaptureSourceKind SelectedSourceKind => _selectedSource.Kind;

    public string SelectedSourceLabel => _selectedSource.DisplayName;

    public string SelectedSourceTitle =>
        SelectedSourceKind switch
        {
            CaptureSourceKind.Display => "Display",
            CaptureSourceKind.Window => "Window",
            CaptureSourceKind.Region => "Region",
            _ => "Display"
        };

    public string SelectedSourceGlyph =>
        SelectedSourceKind switch
        {
            CaptureSourceKind.Display => "\uE7F4",
            CaptureSourceKind.Window => "\uE8A7",
            CaptureSourceKind.Region => "\uE7C8",
            _ => "\uE7F4"
        };

    public string DisplaySourceButtonLabel => DisplaySourceButtonLabelText;

    public string WindowSourceButtonLabel => WindowSourceButtonLabelText;

    public string RegionSourceButtonLabel => RegionSourceButtonLabelText;

    public string SelectedSourcePillLabel => _selectedSource.DisplayName;

    public string SelectedMicrophoneDisplayName
    {
        get
        {
            if (!IsMicrophoneEnabled)
            {
                return "Off";
            }

            var selectedDevice = Microphones.FirstOrDefault(device => device.Id == SelectedMicrophoneDeviceId);
            if (selectedDevice is not null)
            {
                return selectedDevice.DisplayName;
            }

            return Microphones.FirstOrDefault(device => device.IsDefault)?.DisplayName
                ?? Microphones.FirstOrDefault()?.DisplayName
                ?? "Off";
        }
    }

    public string StatusText
    {
        get => _statusText;
        private set
        {
            if (SetProperty(ref _statusText, value))
            {
                OnPropertyChanged(nameof(StatusOverlayText));
            }
        }
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
            if (value && !IsSettingsEnabled)
            {
                return;
            }

            if (SetProperty(ref _isSettingsOpen, value))
            {
                OnPropertyChanged(nameof(SettingsVisibility));
                OnPropertyChanged(nameof(SourceStripVisibility));
                OnPropertyChanged(nameof(CompactSourceStripVisibility));
                OnPropertyChanged(nameof(SettingsSourceStripVisibility));
                OnPropertyChanged(nameof(PrimaryButtonBrush));
                OnPropertyChanged(nameof(SecondaryButtonBrush));
                OnPropertyChanged(nameof(SecondaryGlyphForeground));
                OnPropertyChanged(nameof(MicrophoneButtonBrush));
                OnPropertyChanged(nameof(MicrophoneGlyphForeground));
                OnPropertyChanged(nameof(SettingsButtonBrush));
                OnPropertyChanged(nameof(SettingsGlyphForeground));
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
            RaiseWorkbenchDetailsChanged();
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
            RefreshResolutionOptions();
            OnPropertyChanged();
            PersistSettings();
            RefreshSelectionStatusIfReady();
            RaiseWorkbenchDetailsChanged();
        }
    }

    public ResolutionOptionItem SelectedResolutionOption
    {
        get => ResolutionOptions.FirstOrDefault(option => option.Value == _settings.Resolution)
            ?? ResolutionOptions[0];
        set
        {
            if (value is not null)
            {
                SelectedResolution = value.Value;
            }
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
            RaiseWorkbenchDetailsChanged();
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
            RaiseWorkbenchDetailsChanged();
        }
    }

    public EncoderPreference SelectedEncoderPreference
    {
        get => _settings.EncoderPreference;
        set
        {
            if (_settings.EncoderPreference == value)
            {
                return;
            }

            _settings.EncoderPreference = value;
            OnPropertyChanged();
            PersistSettings();
            RaiseWorkbenchDetailsChanged();
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
            OnPropertyChanged(nameof(SelectedMicrophoneDisplayName));
            OnPropertyChanged(nameof(MicrophoneButtonBrush));
            OnPropertyChanged(nameof(MicrophoneGlyphForeground));
            OnPropertyChanged(nameof(MicrophoneIndicatorVisibility));
            OnPropertyChanged(nameof(MicrophoneDisabledSlashVisibility));
            OnPropertyChanged(nameof(IsMicrophoneDeviceSelectionEnabled));
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
            OnPropertyChanged(nameof(SelectedMicrophoneDisplayName));
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
            OnPropertyChanged(nameof(SaveDirectoryDisplay));
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
            if (value)
            {
                _settings.LastSourceKind = _selectedSource.Kind;
                _settings.LastSourceToken = _selectedSource.RememberToken;
            }
            else
            {
                _settings.LastSourceKind = CaptureSourceKind.Display;
                _settings.LastSourceToken = null;
            }

            OnPropertyChanged();
            PersistSettings();
            RaiseWorkbenchDetailsChanged();
        }
    }

    public bool IsDisplaySelected => SelectedSourceKind == CaptureSourceKind.Display;

    public bool IsWindowSelected => SelectedSourceKind == CaptureSourceKind.Window;

    public bool IsRegionSelected => SelectedSourceKind == CaptureSourceKind.Region;

    public Brush DisplaySourceButtonBackground => GetSourceButtonBackground(CaptureSourceKind.Display);

    public Brush WindowSourceButtonBackground => GetSourceButtonBackground(CaptureSourceKind.Window);

    public Brush RegionSourceButtonBackground => GetSourceButtonBackground(CaptureSourceKind.Region);

    public Brush DisplaySourceButtonBorderBrush => GetSourceButtonBorderBrush(CaptureSourceKind.Display);

    public Brush WindowSourceButtonBorderBrush => GetSourceButtonBorderBrush(CaptureSourceKind.Window);

    public Brush RegionSourceButtonBorderBrush => GetSourceButtonBorderBrush(CaptureSourceKind.Region);

    public Brush DisplaySourceButtonForeground => TextLightBrush;

    public Brush WindowSourceButtonForeground => TextLightBrush;

    public Brush RegionSourceButtonForeground => TextLightBrush;

    public bool IsSourceSelectionEnabled => SessionState is HudSessionState.Idle or HudSessionState.SourceSelected;

    public bool IsMicrophoneToggleEnabled => SessionState is HudSessionState.Idle or HudSessionState.SourceSelected;

    public bool IsSettingsEnabled => SessionState is HudSessionState.Idle or HudSessionState.SourceSelected;

    public bool IsSecondaryActionEnabled => SessionState is HudSessionState.Recording or HudSessionState.Paused;

    public string SecondaryActionGlyph =>
        SessionState switch
        {
            HudSessionState.Recording => "\uE769",
            HudSessionState.Paused => "\uE768",
            _ => string.Empty
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
            _ => "Pause or resume recording"
        };

    public string SessionStateLabel =>
        SessionState switch
        {
            HudSessionState.Idle => "Idle",
            HudSessionState.SourceSelected => "Ready",
            HudSessionState.Countdown => $"Countdown ({_countdownRemaining}s)",
            HudSessionState.Recording => "Recording",
            HudSessionState.Paused => "Paused",
            HudSessionState.StoppingSaving => "Saving",
            _ => "Idle"
        };

    public Visibility StartActionVisibility =>
        SessionState is HudSessionState.Idle or HudSessionState.SourceSelected
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility StopActionVisibility =>
        SessionState is HudSessionState.Countdown or HudSessionState.Recording or HudSessionState.Paused or HudSessionState.StoppingSaving
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility PauseResumeActionVisibility =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused
            ? Visibility.Visible
            : Visibility.Collapsed;

    public bool IsStopActionEnabled => SessionState is HudSessionState.Countdown or HudSessionState.Recording or HudSessionState.Paused;

    public string StopActionLabel =>
        SessionState switch
        {
            HudSessionState.Countdown => "Cancel countdown",
            HudSessionState.StoppingSaving => "Stopping...",
            _ => "Stop recording"
        };

    public string PauseResumeActionLabel =>
        SessionState switch
        {
            HudSessionState.Paused => "Resume",
            _ => "Pause"
        };

    public string SelectedSourceSummaryLabel => $"{SelectedSourceTitle}: {_selectedSource.DisplayName}";

    public string ExpectedCaptureRegionLabel
    {
        get
        {
            var preview = DescribeCurrentSelection();
            return UsesPrecisePreview(_selectedSource.Kind)
                ? FormatRegion(preview.CaptureRegion)
                : $"Requested {FormatRegion(preview.CaptureRegion)}";
        }
    }

    public string ExpectedOutputLabel =>
        UsesPrecisePreview(_selectedSource.Kind)
            ? DescribeCurrentSelection().OutputSizeLabel
            : "Backend-dependent (reported after stop)";

    public string ExpectedAdjustmentsLabel
    {
        get
        {
            var preview = DescribeCurrentSelection();
            if (!UsesPrecisePreview(_selectedSource.Kind))
            {
                return _selectedSource.Kind == CaptureSourceKind.Window
                    ? "Window capture is best-effort in the current backend. Use completed telemetry as the authoritative effective bounds and output."
                    : "Display capture is backend-dependent in the current backend. Use completed telemetry as the authoritative effective bounds and output.";
            }

            if (preview.WasClippedToVirtualDesktop && preview.WasAdjustedToEvenDimensions)
            {
                return "Clipped to virtual desktop and normalized to even dimensions.";
            }

            if (preview.WasClippedToVirtualDesktop)
            {
                return "Clipped to the virtual desktop.";
            }

            if (preview.WasAdjustedToEvenDimensions && preview.WasScaledFromSource)
            {
                return "Scaled for the requested resolution and normalized to even dimensions.";
            }

            if (preview.WasAdjustedToEvenDimensions)
            {
                return "Normalized to even dimensions for the encoder.";
            }

            if (preview.WasScaledFromSource)
            {
                return "Scaled for the requested resolution.";
            }

            return "No clipping or output normalization expected.";
        }
    }

    public string CurrentOutputPathLabel =>
        string.IsNullOrWhiteSpace(_lastRecorderStatus.CurrentOutputPath)
            ? "No output file yet."
            : _lastRecorderStatus.CurrentOutputPath;

    public string TelemetrySummaryLabel
    {
        get
        {
            var telemetry = _lastRecorderStatus.Telemetry;
            if (telemetry is null)
            {
                return SessionState is HudSessionState.Recording or HudSessionState.Paused or HudSessionState.StoppingSaving
                    ? "Telemetry will be available after the recording stops."
                    : "No completed recording telemetry yet.";
            }

            var adapterLabel = string.IsNullOrWhiteSpace(telemetry.AdapterName) ? string.Empty : $" on {telemetry.AdapterName}";
            var vendorLabel = string.IsNullOrWhiteSpace(telemetry.EncoderVendor) || telemetry.EncoderVendor == "unknown"
                ? string.Empty
                : $" {telemetry.EncoderVendor}";
            var encodeMode = telemetry.IsHardwareEncode ? "hardware" : "software/unknown";
            var fallbackLabel = BuildCaptureFallbackLabel(telemetry);
            return
                $"Output {telemetry.OutputWidth}x{telemetry.OutputHeight} at {telemetry.AverageFramesPerSecond:F1} FPS avg via {telemetry.CaptureBackend} + {telemetry.VideoCodec}/{telemetry.EncoderPixelFormat}{vendorLabel} ({encodeMode}){adapterLabel}{fallbackLabel}.";
        }
    }

    public string TelemetryFramesLabel
    {
        get
        {
            var telemetry = _lastRecorderStatus.Telemetry;
            return telemetry is null
                ? "Frames: n/a"
                : $"Frames: captured {telemetry.CapturedFrames}, encoded {telemetry.EncodedFrames}, dropped {telemetry.DroppedFrames}.";
        }
    }

    public string TelemetryLatencyLabel
    {
        get
        {
            var telemetry = _lastRecorderStatus.Telemetry;
            return telemetry is null
                ? "Latency: n/a"
                : $"Latency ms: capture {telemetry.AverageCaptureLatencyMs:F2}, queue {telemetry.AverageQueueLatencyMs:F2}, convert {telemetry.AverageConvertLatencyMs:F2}, encode {telemetry.AverageEncodeLatencyMs:F2}, WGC first frame {telemetry.WgcFirstFrameLatencyMs:F2}.";
        }
    }

    public string TelemetryQueueLabel
    {
        get
        {
            var telemetry = _lastRecorderStatus.Telemetry;
            return telemetry is null
                ? "Queue: n/a"
                : $"Queue: peak depth {telemetry.PeakQueueDepth}, backpressure drops {telemetry.BackpressureDrops}, capture failures {telemetry.CaptureFailureDrops}, pacing overruns {telemetry.PacingOverruns}.";
        }
    }

    public bool IsMicrophoneDeviceSelectionEnabled => IsSettingsEnabled && IsMicrophoneEnabled && Microphones.Count > 0;

    public Brush PrimaryButtonBrush => UsesActiveActionPalette ? AccentBrush : DarkerBrush;

    public Brush PrimaryGlyphForeground => TextLightBrush;

    public Brush SecondaryButtonBrush => UsesActiveActionPalette ? DarkBrush : LightBrush;

    public Brush SecondaryGlyphForeground => UsesActiveActionPalette ? TextLightBrush : TextDarkBrush;

    public Brush MicrophoneButtonBrush => UsesActiveActionPalette ? DarkBrush : LightBrush;

    public Brush MicrophoneGlyphForeground => UsesActiveActionPalette ? TextLightBrush : TextDarkBrush;

    public Brush SettingsButtonBrush => UsesActiveActionPalette ? DarkBrush : LightBrush;

    public Brush SettingsGlyphForeground => UsesActiveActionPalette ? TextLightBrush : TextDarkBrush;

    public Brush SourceIndicatorBrush => AccentBrush;

    public Brush MicrophoneIndicatorBrush => IndicatorOnBrush;

    public Brush SecondaryIndicatorBrush => IndicatorOnBrush;

    public Visibility FeedbackVisibility => FeedbackState == HudFeedbackState.None ? Visibility.Collapsed : Visibility.Visible;

    public Visibility SettingsVisibility => IsSettingsOpen ? Visibility.Visible : Visibility.Collapsed;

    public Visibility SecondaryActionVisibility =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility SourceStripVisibility => SettingsSourceStripVisibility;

    public Visibility CompactSourceStripVisibility =>
        !IsSettingsOpen && SessionState is HudSessionState.Idle or HudSessionState.SourceSelected
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility SettingsSourceStripVisibility => IsSettingsOpen ? Visibility.Visible : Visibility.Collapsed;

    public Visibility StatusVisibility =>
        SessionState is HudSessionState.Countdown or HudSessionState.Paused or HudSessionState.StoppingSaving
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility MicrophoneIndicatorVisibility =>
        (SessionState is HudSessionState.Recording or HudSessionState.Paused) && IsMicrophoneEnabled
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility SecondaryIndicatorVisibility =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility MicrophoneDisabledSlashVisibility => IsMicrophoneEnabled ? Visibility.Collapsed : Visibility.Visible;

    public Visibility PrimaryIdleGlyphVisibility =>
        SessionState is HudSessionState.Idle or HudSessionState.SourceSelected or HudSessionState.StoppingSaving
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility PrimaryStopGlyphVisibility =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused
            ? Visibility.Visible
            : Visibility.Collapsed;

    public Visibility PrimaryCountdownVisibility => SessionState == HudSessionState.Countdown ? Visibility.Visible : Visibility.Collapsed;

    public Brush FeedbackBrush => WarningBrush;

    public Brush FeedbackForegroundBrush => TextLightBrush;

    public string CountdownLabel => _countdownRemaining > 0 ? _countdownRemaining.ToString() : string.Empty;

    public string StatusOverlayText =>
        SessionState == HudSessionState.Countdown
            ? $"Starting in {_countdownRemaining}s"
            : StatusText;

    public string SaveDirectoryDisplay
    {
        get
        {
            var trimmed = SaveDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            var leaf = Path.GetFileName(trimmed);
            return string.IsNullOrWhiteSpace(leaf) ? trimmed : $"{leaf}/";
        }
    }

    public async Task InitializeAsync()
    {
        if (_isInitialized)
        {
            return;
        }

        try
        {
            _settings = await _settingsStore.LoadAsync();
            RaiseSettingsChanged();
            await LoadMicrophonesAsync();

            var initialSourceKind = _settings.RememberLastSource
                ? _settings.LastSourceKind
                : CaptureSourceKind.Display;
            var initialToken = _settings.RememberLastSource ? _settings.LastSourceToken : null;
            var initialSource = _captureSourcePicker.ResolveInitialSource(initialSourceKind, initialToken);

            ApplySelectedSource(initialSource, persistSelection: false);
            await _recorderController.InitializeAsync();
            _lastRecorderStatus = _recorderController.CurrentStatus;

            _isInitialized = true;
            InitializeCommand.RaiseCanExecuteChanged();
            RefreshSelectionStatusIfReady();
            RaiseWorkbenchDetailsChanged();
        }
        catch (Exception ex)
        {
            ShowNonBlockingError($"Recorder initialization failed. {ex.Message}");
            StatusText = "Recorder initialization failed.";
        }
    }

    public Task ShutdownAsync()
    {
        _countdownCts?.Cancel();
        _recorderController.StatusChanged -= RecorderControllerOnStatusChanged;
        return PersistSettingsCoreAsync();
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
                try
                {
                    var result = await _recorderController.StopAsync();
                    ApplyCurrentRecorderStatus();
                    if (!result.WasSuccessful)
                    {
                        ShowNonBlockingFeedback(result.ErrorMessage ?? "Recording failed.");
                    }
                }
                catch (Exception ex)
                {
                    ApplyCurrentRecorderStatus();
                    ShowNonBlockingError($"Couldn't stop the recording. {ex.Message}");
                }

                return;
            default:
                await BeginRecordingFlowAsync();
                return;
        }
    }

    public async Task ExecuteSecondaryActionAsync()
    {
        if (SessionState is HudSessionState.Recording or HudSessionState.Paused)
        {
            await TogglePauseResumeAsync();
        }
    }

    public async Task TogglePauseResumeAsync()
    {
        try
        {
            if (SessionState == HudSessionState.Recording)
            {
                SessionState = HudStateReducer.Reduce(SessionState, HudActionType.RequestPause);
                await _recorderController.PauseAsync();
            }
            else if (SessionState == HudSessionState.Paused)
            {
                SessionState = HudStateReducer.Reduce(SessionState, HudActionType.RequestResume);
                await _recorderController.ResumeAsync();
            }
        }
        catch (Exception ex)
        {
            ApplyCurrentRecorderStatus();
            ShowNonBlockingError($"Couldn't update the recording state. {ex.Message}");
        }
    }

    private async Task BeginRecordingFlowAsync()
    {
        IsSettingsOpen = false;
        FeedbackState = HudFeedbackState.None;
        FeedbackText = string.Empty;

        if (_settings.Countdown == CountdownOption.Off)
        {
            StatusText = BuildStartMessage();
            try
            {
                await _recorderController.StartAsync(_selectedSource, _settings.ToRecordingOptions());
            }
            catch (Exception ex)
            {
                ApplyCurrentRecorderStatus();
                ShowNonBlockingError($"Couldn't start the recording. {ex.Message}");
            }

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
                await Task.Delay(TimeSpan.FromSeconds(1), _countdownCts.Token);
                _countdownRemaining--;
            }

            SessionState = HudStateReducer.Reduce(SessionState, HudActionType.CountdownCompleted);
            RaiseComputedStateChanged();
            try
            {
                StatusText = BuildStartMessage();
                await _recorderController.StartAsync(_selectedSource, _settings.ToRecordingOptions(), _countdownCts.Token);
            }
            catch (Exception ex)
            {
                ApplyCurrentRecorderStatus();
                ShowNonBlockingError($"Couldn't start the recording. {ex.Message}");
            }
        }
        catch (OperationCanceledException)
        {
            _countdownRemaining = 0;
            RaiseComputedStateChanged();
        }
    }

    private async Task SelectDisplaySourceAsync() =>
        await PickSourceAsync(
            static (picker, currentSource, cancellationToken) => picker.SelectDisplaySourceAsync(currentSource, cancellationToken),
            "No displays are currently available.");

    private async Task SelectWindowSourceAsync() =>
        await PickSourceAsync(
            static (picker, currentSource, cancellationToken) => picker.SelectWindowSourceAsync(currentSource, cancellationToken),
            "No capturable windows are currently available.");

    private async Task SelectRegionSourceAsync() =>
        await PickSourceAsync(
            static (picker, currentSource, cancellationToken) => picker.SelectRegionSourceAsync(currentSource, cancellationToken),
            "Region selection was cancelled.");

    private async Task PickSourceAsync(
        Func<ICaptureSourcePicker, CaptureSourceDescriptor?, CancellationToken, Task<CaptureSourceDescriptor?>> pickerAction,
        string nullResultMessage)
    {
        if (!IsSourceSelectionEnabled)
        {
            return;
        }

        try
        {
            IsSettingsOpen = false;
            var nextSource = await pickerAction(_captureSourcePicker, _selectedSource, CancellationToken.None);
            if (nextSource is null)
            {
                StatusText = nullResultMessage;
                return;
            }

            ApplySelectedSource(nextSource);
        }
        catch (InvalidOperationException ex)
        {
            ShowNonBlockingError(string.IsNullOrWhiteSpace(ex.Message) ? "Source selection failed." : ex.Message);
        }
        catch (OperationCanceledException)
        {
            StatusText = nullResultMessage;
        }
        catch (Exception ex)
        {
            ShowNonBlockingError($"Source selection failed. {ex.Message}");
        }
    }

    private void ApplySelectedSource(CaptureSourceDescriptor source, bool persistSelection = true)
    {
        _selectedSource = source;
        if (_settings.RememberLastSource)
        {
            _settings.LastSourceKind = source.Kind;
            _settings.LastSourceToken = source.RememberToken;
        }

        if (SessionState is HudSessionState.Idle or HudSessionState.SourceSelected)
        {
            SessionState = HudStateReducer.Reduce(SessionState, HudActionType.SelectSource);
        }

        RefreshSourceSelectionVisuals();
        RefreshSelectionStatusIfReady();

        if (persistSelection && _settings.RememberLastSource)
        {
            PersistSettings();
        }
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

            OnPropertyChanged(nameof(SelectedMicrophoneDisplayName));
            OnPropertyChanged(nameof(IsMicrophoneDeviceSelectionEnabled));
        });
    }

    private void RecorderControllerOnStatusChanged(object? sender, RecorderStatusSnapshot snapshot)
    {
        _dispatcherQueue.TryEnqueue(() =>
        {
            _lastRecorderStatus = snapshot;
            UpdateSelectedSource(snapshot.ActiveSource);

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
                case RecorderState.ErrorNonBlocking:
                    FeedbackState = HudFeedbackState.ErrorNonBlocking;
                    FeedbackText = snapshot.Message ?? "Recorder warning.";
                    _ = DismissFeedbackAsync();
                    break;
            }

            StatusText = snapshot.Message ?? StatusText;
            RaiseWorkbenchDetailsChanged();
            RaiseComputedStateChanged();
        });
    }

    private async Task DismissFeedbackAsync()
    {
        await Task.Delay(FeedbackDisplayDuration).ConfigureAwait(false);
        _dispatcherQueue.TryEnqueue(() =>
        {
            FeedbackState = HudFeedbackState.None;
            FeedbackText = string.Empty;
        });
    }

    private void RaiseSettingsChanged()
    {
        OnPropertyChanged(nameof(SelectedSourceTitle));
        OnPropertyChanged(nameof(SelectedSourceGlyph));
        OnPropertyChanged(nameof(SelectedSourcePillLabel));
        OnPropertyChanged(nameof(SelectedFrameRate));
        OnPropertyChanged(nameof(SelectedResolution));
        RefreshResolutionOptions();
        OnPropertyChanged(nameof(SelectedQuality));
        OnPropertyChanged(nameof(SelectedCountdown));
        OnPropertyChanged(nameof(SelectedEncoderPreference));
        OnPropertyChanged(nameof(IsSystemAudioEnabled));
        OnPropertyChanged(nameof(IsMicrophoneEnabled));
        OnPropertyChanged(nameof(SelectedMicrophoneDeviceId));
        OnPropertyChanged(nameof(SelectedMicrophoneDisplayName));
        OnPropertyChanged(nameof(SaveDirectory));
        OnPropertyChanged(nameof(SaveDirectoryDisplay));
        OnPropertyChanged(nameof(RememberLastSource));
        OnPropertyChanged(nameof(IsMicrophoneDeviceSelectionEnabled));
        OnPropertyChanged(nameof(MicrophoneButtonBrush));
        OnPropertyChanged(nameof(MicrophoneGlyphForeground));
        OnPropertyChanged(nameof(MicrophoneIndicatorVisibility));
        OnPropertyChanged(nameof(MicrophoneDisabledSlashVisibility));
        RaiseWorkbenchDetailsChanged();
    }

    private void RaiseComputedStateChanged()
    {
        if (!IsSettingsEnabled && IsSettingsOpen)
        {
            IsSettingsOpen = false;
        }

        OnPropertyChanged(nameof(IsSourceSelectionEnabled));
        OnPropertyChanged(nameof(IsMicrophoneToggleEnabled));
        OnPropertyChanged(nameof(IsSettingsEnabled));
        OnPropertyChanged(nameof(IsSecondaryActionEnabled));
        OnPropertyChanged(nameof(SecondaryActionGlyph));
        OnPropertyChanged(nameof(SecondaryActionToolTip));
        OnPropertyChanged(nameof(SessionStateLabel));
        OnPropertyChanged(nameof(StartActionVisibility));
        OnPropertyChanged(nameof(StopActionVisibility));
        OnPropertyChanged(nameof(PauseResumeActionVisibility));
        OnPropertyChanged(nameof(IsStopActionEnabled));
        OnPropertyChanged(nameof(StopActionLabel));
        OnPropertyChanged(nameof(PauseResumeActionLabel));
        OnPropertyChanged(nameof(PrimaryActionGlyph));
        OnPropertyChanged(nameof(PrimaryButtonBrush));
        OnPropertyChanged(nameof(PrimaryGlyphForeground));
        OnPropertyChanged(nameof(SecondaryButtonBrush));
        OnPropertyChanged(nameof(SecondaryGlyphForeground));
        OnPropertyChanged(nameof(SettingsButtonBrush));
        OnPropertyChanged(nameof(SettingsGlyphForeground));
        OnPropertyChanged(nameof(CountdownLabel));
        OnPropertyChanged(nameof(SourceStripVisibility));
        OnPropertyChanged(nameof(CompactSourceStripVisibility));
        OnPropertyChanged(nameof(SettingsSourceStripVisibility));
        OnPropertyChanged(nameof(SecondaryActionVisibility));
        OnPropertyChanged(nameof(StatusVisibility));
        OnPropertyChanged(nameof(StatusOverlayText));
        OnPropertyChanged(nameof(MicrophoneIndicatorVisibility));
        OnPropertyChanged(nameof(SecondaryIndicatorVisibility));
        OnPropertyChanged(nameof(PrimaryIdleGlyphVisibility));
        OnPropertyChanged(nameof(PrimaryStopGlyphVisibility));
        OnPropertyChanged(nameof(PrimaryCountdownVisibility));
        OnPropertyChanged(nameof(IsMicrophoneDeviceSelectionEnabled));
        PrimaryActionCommand.RaiseCanExecuteChanged();
        SecondaryActionCommand.RaiseCanExecuteChanged();
        SelectDisplaySourceCommand.RaiseCanExecuteChanged();
        SelectWindowSourceCommand.RaiseCanExecuteChanged();
        SelectRegionSourceCommand.RaiseCanExecuteChanged();
    }

    private void PersistSettings()
    {
        _ = PersistSettingsCoreAsync();
    }

    private void ApplyCurrentRecorderStatus()
    {
        RecorderControllerOnStatusChanged(this, _recorderController.CurrentStatus);
    }

    private void UpdateSelectedSource(CaptureSourceDescriptor? source)
    {
        if (source is null)
        {
            return;
        }

        _selectedSource = source;
        RefreshSourceSelectionVisuals();
    }

    private void RefreshSourceSelectionVisuals()
    {
        OnPropertyChanged(nameof(SelectedSourceKind));
        OnPropertyChanged(nameof(SelectedSourceLabel));
        OnPropertyChanged(nameof(SelectedSourceTitle));
        OnPropertyChanged(nameof(SelectedSourceGlyph));
        OnPropertyChanged(nameof(SelectedSourcePillLabel));
        OnPropertyChanged(nameof(SelectedSourceSummaryLabel));
        OnPropertyChanged(nameof(IsDisplaySelected));
        OnPropertyChanged(nameof(IsWindowSelected));
        OnPropertyChanged(nameof(IsRegionSelected));
        RefreshResolutionOptions();
        RaiseSourceButtonVisualsChanged();
        RaiseWorkbenchDetailsChanged();
    }

    private void RefreshSelectionStatusIfReady()
    {
        if (SessionState is not (HudSessionState.Idle or HudSessionState.SourceSelected))
        {
            return;
        }

        StatusText = BuildReadyMessage();
    }

    private string BuildReadyMessage()
    {
        var preview = _captureSourcePicker.Describe(_selectedSource, _settings.ToRecordingOptions());
        if (!UsesPrecisePreview(_selectedSource.Kind))
        {
            return $"{_selectedSource.DisplayName} selected. Requested bounds {FormatRegion(preview.CaptureRegion)}. Effective output is backend-dependent and is confirmed after stop.";
        }

        return $"{_selectedSource.DisplayName} selected. Bounds {FormatRegion(preview.CaptureRegion)}. Output {preview.OutputSizeLabel}{BuildPreviewSuffix(preview)}.";
    }

    private string BuildStartMessage()
    {
        var preview = _captureSourcePicker.Describe(_selectedSource, _settings.ToRecordingOptions());
        if (!UsesPrecisePreview(_selectedSource.Kind))
        {
            return $"Starting {_selectedSource.DisplayName}. Requested bounds {FormatRegion(preview.CaptureRegion)}. Effective output will be reported after stop.";
        }

        return $"Starting {_selectedSource.DisplayName}. Bounds {FormatRegion(preview.CaptureRegion)}. Output {preview.OutputSizeLabel}{BuildPreviewSuffix(preview)}.";
    }

    private static string BuildPreviewSuffix(CaptureSourcePreview preview)
    {
        if (preview.WasClippedToVirtualDesktop && preview.WasAdjustedToEvenDimensions)
        {
            return " after clipping to the virtual desktop and even normalization";
        }

        if (preview.WasClippedToVirtualDesktop)
        {
            return " after clipping to the virtual desktop";
        }

        if (preview.WasAdjustedToEvenDimensions)
        {
            return " after even normalization";
        }

        if (preview.WasScaledFromSource)
        {
            return " after scaling";
        }

        return string.Empty;
    }

    private CaptureSourcePreview DescribeCurrentSelection() =>
        _captureSourcePicker.Describe(_selectedSource, _settings.ToRecordingOptions());

    private static bool UsesPrecisePreview(CaptureSourceKind kind) =>
        kind is CaptureSourceKind.Display or CaptureSourceKind.Region;

    private IReadOnlyList<ResolutionOptionItem> BuildResolutionOptions() =>
    [
        new(ResolutionOption.Auto, BuildAutoResolutionDisplayName()),
        new(ResolutionOption.P480, "480p"),
        new(ResolutionOption.P720, "720p"),
        new(ResolutionOption.P1080, "1080p"),
        new(ResolutionOption.P1440, "1440p"),
        new(ResolutionOption.P2160, "4K")
    ];

    private void RefreshResolutionOptions()
    {
        ResolutionOptions = BuildResolutionOptions();
        OnPropertyChanged(nameof(ResolutionOptions));
        OnPropertyChanged(nameof(SelectedResolutionOption));
    }

    private string BuildAutoResolutionDisplayName()
    {
        try
        {
            var autoOptions = _settings.ToRecordingOptions() with { Resolution = ResolutionOption.Auto };
            var preview = _captureSourcePicker.Describe(_selectedSource, autoOptions);
            return $"Native {FormatResolutionHeight(preview.OutputHeight)}";
        }
        catch
        {
            return "Auto";
        }
    }

    private static string FormatResolutionHeight(int height) =>
        height >= 2160 ? "4K" : $"{height}p";

    private static string BuildCaptureFallbackLabel(RecordingSessionTelemetry telemetry)
    {
        if (string.Equals(telemetry.CaptureFallbackReason, "none", StringComparison.OrdinalIgnoreCase) ||
            string.IsNullOrWhiteSpace(telemetry.CaptureFallbackTo))
        {
            return string.Empty;
        }

        var from = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackFrom)
            ? telemetry.RequestedCaptureBackend
            : telemetry.CaptureFallbackFrom;
        var to = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackTo)
            ? telemetry.CaptureBackend
            : telemetry.CaptureFallbackTo;
        var hresult = string.IsNullOrWhiteSpace(telemetry.CaptureFallbackHresult)
            ? string.Empty
            : $" {telemetry.CaptureFallbackHresult}";

        return $"; fallback {from} -> {to} ({telemetry.CaptureFallbackReason}{hresult})";
    }

    private void RaiseWorkbenchDetailsChanged()
    {
        OnPropertyChanged(nameof(SelectedSourceSummaryLabel));
        OnPropertyChanged(nameof(ExpectedCaptureRegionLabel));
        OnPropertyChanged(nameof(ExpectedOutputLabel));
        OnPropertyChanged(nameof(ExpectedAdjustmentsLabel));
        OnPropertyChanged(nameof(CurrentOutputPathLabel));
        OnPropertyChanged(nameof(TelemetrySummaryLabel));
        OnPropertyChanged(nameof(TelemetryFramesLabel));
        OnPropertyChanged(nameof(TelemetryLatencyLabel));
        OnPropertyChanged(nameof(TelemetryQueueLabel));
    }

    private static string FormatRegion(ScreenRegion region) =>
        $"{region.Width}x{region.Height} @ ({region.X}, {region.Y})";

    private void ShowNonBlockingError(string message)
    {
        FeedbackState = HudFeedbackState.ErrorNonBlocking;
        FeedbackText = message;
        StatusText = message;
        _ = DismissFeedbackAsync();
    }

    private void ShowNonBlockingFeedback(string message)
    {
        FeedbackState = HudFeedbackState.ErrorNonBlocking;
        FeedbackText = message;
        _ = DismissFeedbackAsync();
    }

    private async Task PersistSettingsCoreAsync()
    {
        try
        {
            await _settingsStore.SaveAsync(CloneSettings(_settings)).ConfigureAwait(false);
        }
        catch
        {
            // Settings persistence is best-effort while the current slice continues to evolve.
        }
    }

    private static AppSettings CloneSettings(AppSettings settings) =>
        new()
        {
            Countdown = settings.Countdown,
            FrameRate = settings.FrameRate,
            LastSourceKind = settings.LastSourceKind,
            LastSourceToken = settings.LastSourceToken,
            MicrophoneDeviceId = settings.MicrophoneDeviceId,
            MicrophoneEnabled = settings.MicrophoneEnabled,
            QualityPreset = settings.QualityPreset,
            EncoderPreference = settings.EncoderPreference,
            VideoCodec = settings.VideoCodec,
            RememberLastSource = settings.RememberLastSource,
            Resolution = settings.Resolution,
            SaveDirectory = settings.SaveDirectory,
            SystemAudioEnabled = settings.SystemAudioEnabled
        };

    private Brush GetSourceButtonBackground(CaptureSourceKind kind) =>
        SelectedSourceKind == kind ? SelectedSourceBrush : DarkerBrush;

    private Brush GetSourceButtonBorderBrush(CaptureSourceKind kind) =>
        SelectedSourceKind == kind ? SelectedSourceStrokeBrush : TransparentBrush;

    private bool UsesActiveActionPalette =>
        SessionState is HudSessionState.Recording or HudSessionState.Paused;

    private void RaiseSourceButtonVisualsChanged()
    {
        OnPropertyChanged(nameof(DisplaySourceButtonBackground));
        OnPropertyChanged(nameof(WindowSourceButtonBackground));
        OnPropertyChanged(nameof(RegionSourceButtonBackground));
        OnPropertyChanged(nameof(DisplaySourceButtonBorderBrush));
        OnPropertyChanged(nameof(WindowSourceButtonBorderBrush));
        OnPropertyChanged(nameof(RegionSourceButtonBorderBrush));
        OnPropertyChanged(nameof(DisplaySourceButtonForeground));
        OnPropertyChanged(nameof(WindowSourceButtonForeground));
        OnPropertyChanged(nameof(RegionSourceButtonForeground));
    }

    private static SolidColorBrush CreateBrush(byte r, byte g, byte b, byte a = 255) =>
        new(Color.FromArgb(a, r, g, b));
}

public sealed record ResolutionOptionItem(ResolutionOption Value, string DisplayName);
