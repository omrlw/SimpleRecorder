using Microsoft.Extensions.DependencyInjection;
using SimpleRecorder.Contracts.Services;
using SimpleRecorder.Infrastructure.Audio;
using SimpleRecorder.Infrastructure.Native;
using SimpleRecorder.Infrastructure.Settings;
using SimpleRecorder.Infrastructure.SourceSelection;
using SimpleRecorder.Infrastructure.Tray;
using SimpleRecorder.Presentation.ViewModels;

namespace SimpleRecorder.App.Bootstrap;

internal static class CompositionRoot
{
    internal static ServiceProvider BuildServices()
    {
        var services = new ServiceCollection();

        services.AddSingleton<ISettingsStore, JsonSettingsStore>();
        services.AddSingleton<IAudioDeviceCatalog, AudioDeviceCatalog>();
        services.AddSingleton<ICaptureSourcePicker, CaptureSourcePicker>();
        services.AddSingleton<IRecorderController, NativeRecorderController>();
        services.AddSingleton<ITrayService, TrayIconService>();
        services.AddSingleton<HudViewModel>();

        return services.BuildServiceProvider();
    }
}
