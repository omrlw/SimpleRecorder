using System.Text.Json;
using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;
using Windows.Storage;

namespace SimpleRecorder.Infrastructure.Settings;

public sealed class JsonSettingsStore : ISettingsStore
{
    private const string PackageName = "SimpleRecorder.App";
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly string _settingsPath;

    public JsonSettingsStore()
    {
        var root = ResolveSettingsRoot();

        Directory.CreateDirectory(root);
        _settingsPath = Path.Combine(root, "settings.json");
        TryMigrateLegacySettings(_settingsPath);
    }

    public async Task<AppSettings> LoadAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!File.Exists(_settingsPath))
            {
                var defaults = NormalizeForCurrentSlice(new AppSettings(), out _);
                await SaveCoreAsync(defaults, cancellationToken).ConfigureAwait(false);
                return defaults;
            }

            await using var stream = File.OpenRead(_settingsPath);
            var settings = await JsonSerializer.DeserializeAsync(
                    stream,
                    SettingsSerializerContext.Default.AppSettings,
                    cancellationToken)
                .ConfigureAwait(false);

            var normalized = NormalizeForCurrentSlice(settings ?? new AppSettings(), out var wasNormalized);
            if (settings is null || wasNormalized)
            {
                await SaveCoreAsync(normalized, cancellationToken).ConfigureAwait(false);
            }

            return normalized;
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task SaveAsync(AppSettings settings, CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await SaveCoreAsync(NormalizeForCurrentSlice(settings, out _), cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    private async Task SaveCoreAsync(AppSettings settings, CancellationToken cancellationToken)
    {
        await using var stream = File.Create(_settingsPath);
        await JsonSerializer.SerializeAsync(
                stream,
                settings,
                SettingsSerializerContext.Default.AppSettings,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static string ResolveSettingsRoot()
    {
        try
        {
            return ApplicationData.Current.LocalFolder.Path;
        }
        catch
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Packages",
                PackageName,
                "LocalState");
        }
    }

    private static void TryMigrateLegacySettings(string destinationPath)
    {
        if (File.Exists(destinationPath))
        {
            return;
        }

        var legacyPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "SimpleRecorder",
            "settings.json");

        if (!File.Exists(legacyPath))
        {
            return;
        }

        File.Copy(legacyPath, destinationPath, overwrite: false);
    }

    private static AppSettings NormalizeForCurrentSlice(AppSettings settings, out bool wasNormalized)
    {
        var normalizedFrameRate = NormalizeFrameRate(settings.FrameRate);
        var normalizedResolution = NormalizeResolution(settings.Resolution);
        var needsNormalization =
            settings.FrameRate != normalizedFrameRate ||
            settings.Resolution != normalizedResolution ||
            settings.SystemAudioEnabled ||
            settings.MicrophoneEnabled ||
            settings.MicrophoneDeviceId is not null ||
            settings.VideoCodec != VideoCodec.H264;
        wasNormalized = needsNormalization;
        if (!needsNormalization)
        {
            return settings;
        }

        settings.FrameRate = normalizedFrameRate;
        settings.Resolution = normalizedResolution;
        settings.SystemAudioEnabled = false;
        settings.MicrophoneEnabled = false;
        settings.MicrophoneDeviceId = null;
        settings.VideoCodec = VideoCodec.H264;
        return settings;
    }

    private static FrameRateOption NormalizeFrameRate(FrameRateOption frameRate) =>
        frameRate switch
        {
            FrameRateOption.Monitor or
            FrameRateOption.Fps24 or
            FrameRateOption.Fps30 or
            FrameRateOption.Fps60 => frameRate,
            _ when (int)frameRate > (int)FrameRateOption.Fps60 => FrameRateOption.Monitor,
            _ => FrameRateOption.Fps60
        };

    private static ResolutionOption NormalizeResolution(ResolutionOption resolution) =>
        resolution switch
        {
            ResolutionOption.Auto or
            ResolutionOption.P480 or
            ResolutionOption.P720 or
            ResolutionOption.P1080 or
            ResolutionOption.P1440 or
            ResolutionOption.P2160 => resolution,
            _ => ResolutionOption.Auto
        };
}
