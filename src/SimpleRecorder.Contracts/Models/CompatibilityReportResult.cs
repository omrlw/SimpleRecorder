namespace SimpleRecorder.Contracts.Models;

public sealed record CompatibilityReportResult(
    bool IsSuccessful,
    string ReportPath,
    string? ErrorMessage = null);
