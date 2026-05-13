using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Contracts.Services;

public interface ICompatibilityReportService
{
    Task<CompatibilityReportResult> GenerateAsync(CancellationToken cancellationToken = default);
}
