using System.Text.Json;
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
        var needsNormalization = settings.SystemAudioEnabled || settings.MicrophoneEnabled || settings.MicrophoneDeviceId is not null;
        wasNormalized = needsNormalization;
        if (!needsNormalization)
        {
            return settings;
        }

        settings.SystemAudioEnabled = false;
        settings.MicrophoneEnabled = false;
        settings.MicrophoneDeviceId = null;
        return settings;
    }
}
