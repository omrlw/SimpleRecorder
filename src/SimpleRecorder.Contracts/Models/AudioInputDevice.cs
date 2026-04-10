namespace SimpleRecorder.Contracts.Models;

public sealed record AudioInputDevice(string Id, string DisplayName, bool IsDefault, bool IsAvailable = true);
