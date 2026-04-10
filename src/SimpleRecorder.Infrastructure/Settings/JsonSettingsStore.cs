using System.Text.Json;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;

namespace SimpleRecorder.Infrastructure.Settings;

public sealed class JsonSettingsStore : ISettingsStore
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly string _settingsPath;

    public JsonSettingsStore()
    {
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "SimpleRecorder");

        Directory.CreateDirectory(root);
        _settingsPath = Path.Combine(root, "settings.json");
    }

    public async Task<AppSettings> LoadAsync(CancellationToken cancellationToken = default)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!File.Exists(_settingsPath))
            {
                var defaults = new AppSettings();
                await SaveCoreAsync(defaults, cancellationToken).ConfigureAwait(false);
                return defaults;
            }

            await using var stream = File.OpenRead(_settingsPath);
            var settings = await JsonSerializer.DeserializeAsync(
                    stream,
                    SettingsSerializerContext.Default.AppSettings,
                    cancellationToken)
                .ConfigureAwait(false);

            return settings ?? new AppSettings();
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
            await SaveCoreAsync(settings, cancellationToken).ConfigureAwait(false);
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
}
