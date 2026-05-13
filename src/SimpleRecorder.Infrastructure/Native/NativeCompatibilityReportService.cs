using System.Runtime.InteropServices;
using System.Text.Json;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;

namespace SimpleRecorder.Infrastructure.Native;

public sealed class NativeCompatibilityReportService : ICompatibilityReportService
{
    public Task<CompatibilityReportResult> GenerateAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var reportPath = BuildReportPath();
        Directory.CreateDirectory(Path.GetDirectoryName(reportPath)!);

        nint engine = nint.Zero;
        try
        {
            var nativeAbiVersion = NativeMethods.GetAbiVersion();
            if (nativeAbiVersion != NativeMethods.CurrentAbiVersion)
            {
                var message = $"ABI mismatch. Managed expects {NativeMethods.CurrentAbiVersion}, native reported {nativeAbiVersion}.";
                WriteManagedFailureReport(reportPath, message);
                return Task.FromResult(new CompatibilityReportResult(false, reportPath, message));
            }

            engine = NativeMethods.Create();
            if (engine == nint.Zero)
            {
                const string message = "sr_engine_create returned a null engine handle.";
                WriteManagedFailureReport(reportPath, message);
                return Task.FromResult(new CompatibilityReportResult(false, reportPath, message));
            }

            var result = (NativeMethods.SrResultCode)NativeMethods.WriteCompatibilityReport(engine, reportPath);
            if (result == NativeMethods.SrResultCode.Ok)
            {
                return Task.FromResult(new CompatibilityReportResult(true, reportPath));
            }

            var failure = $"Native compatibility report failed with {result}.";
            WriteManagedFailureReport(reportPath, failure);
            return Task.FromResult(new CompatibilityReportResult(false, reportPath, failure));
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            var message = $"Native compatibility report is unavailable. {ex.Message}";
            WriteManagedFailureReport(reportPath, message);
            return Task.FromResult(new CompatibilityReportResult(false, reportPath, message));
        }
        finally
        {
            if (engine != nint.Zero)
            {
                NativeMethods.Destroy(engine);
            }
        }
    }

    private static string BuildReportPath()
    {
        var root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.MyVideos),
            "SimpleRecorder",
            "compatibility");
        var timestamp = DateTimeOffset.Now.ToString("yyyyMMdd-HHmmss");
        return Path.Combine(root, $"simple-recorder-compatibility-{timestamp}.json");
    }

    private static void WriteManagedFailureReport(string reportPath, string message)
    {
        var report = new
        {
            schemaVersion = 1,
            artifactType = "simple-recorder-compatibility-report",
            generatedAtUnixMillis = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            nativeReportAvailable = false,
            errorMessage = message
        };

        File.WriteAllText(
            reportPath,
            JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
    }
}
