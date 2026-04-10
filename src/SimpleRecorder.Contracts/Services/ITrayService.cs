using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Contracts.Services;

public interface ITrayService : IDisposable
{
    event EventHandler<TrayCommand>? CommandInvoked;

    void Initialize();

    void Update(RecorderStatusSnapshot snapshot);
}
