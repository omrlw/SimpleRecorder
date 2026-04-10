using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Contracts.Services;

public interface IAudioDeviceCatalog
{
    Task<IReadOnlyList<AudioInputDevice>> GetInputDevicesAsync(CancellationToken cancellationToken = default);
}
