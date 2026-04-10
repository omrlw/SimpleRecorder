namespace SimpleRecorder.Contracts.Models;

public sealed record ScreenshotResult(bool WasSuccessful, string? OutputPath, string? ErrorMessage = null);
