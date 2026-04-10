using SimpleRecorder.Contracts.Enums;

namespace SimpleRecorder.Contracts.Models;

public sealed record RecorderStatusSnapshot(
    RecorderState State,
    CaptureSourceDescriptor? ActiveSource,
    string? CurrentOutputPath = null,
    string? Message = null,
    int CountdownRemainingSeconds = 0,
    DateTimeOffset? StartedAtUtc = null);
