using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;
using Windows.Devices.Enumeration;
using Windows.Media.Devices;

namespace SimpleRecorder.Infrastructure.Audio;

public sealed class AudioDeviceCatalog : IAudioDeviceCatalog
{
    public async Task<IReadOnlyList<AudioInputDevice>> GetInputDevicesAsync(CancellationToken cancellationToken = default)
    {
        try
        {
            var selector = MediaDevice.GetAudioCaptureSelector();
            var defaultId = MediaDevice.GetDefaultAudioCaptureId(AudioDeviceRole.Default);
            var devices = await DeviceInformation.FindAllAsync(selector).AsTask(cancellationToken).ConfigureAwait(false);

            var items = devices
                .Select(device => new AudioInputDevice(
                    device.Id,
                    string.IsNullOrWhiteSpace(device.Name) ? "Microphone" : device.Name,
                    string.Equals(device.Id, defaultId, StringComparison.OrdinalIgnoreCase)))
                .ToArray();

            if (items.Length > 0)
            {
                return items;
            }
        }
        catch
        {
            // Falling back to a deterministic placeholder keeps the current slice stable on machines
            // without full device access while the native audio path is still stubbed.
        }

        return
        [
            new AudioInputDevice("default-mic", "System default microphone", true)
        ];
    }
}
