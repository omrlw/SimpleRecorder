namespace SimpleRecorder.Contracts.Models;

public sealed record RecordingResult(
    bool WasSuccessful,
    string? OutputPath,
    TimeSpan Duration,
    string? ErrorMessage = null,
    RecordingSessionTelemetry? Telemetry = null);
