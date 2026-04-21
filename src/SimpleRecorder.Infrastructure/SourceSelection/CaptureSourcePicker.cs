using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;

namespace SimpleRecorder.Infrastructure.SourceSelection;

public sealed class CaptureSourcePicker : ICaptureSourcePicker
{
    public Task<CaptureSourceDescriptor?> SelectDisplaySourceAsync(
        CaptureSourceDescriptor? currentSource,
        CancellationToken cancellationToken = default) =>
        Task.Run(() => SelectNextDisplaySource(currentSource), cancellationToken);

    public Task<CaptureSourceDescriptor?> SelectWindowSourceAsync(
        CaptureSourceDescriptor? currentSource,
        CancellationToken cancellationToken = default) =>
        Task.Run(() => SelectNextWindowSource(currentSource), cancellationToken);

    public async Task<CaptureSourceDescriptor?> SelectRegionSourceAsync(
        CaptureSourceDescriptor? currentSource,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var initialRegion = currentSource?.Kind == CaptureSourceKind.Region
            ? currentSource.Region ?? currentSource.Bounds
            : null;

        var region = await RegionSelectionOverlay
            .PickAsync(initialRegion, cancellationToken)
            .ConfigureAwait(false);

        return region is null
            ? null
            : CreateRegionSource(region);
    }

    public CaptureSourceDescriptor ResolveInitialSource(CaptureSourceKind preferredKind, string? rememberToken)
    {
        var restored = preferredKind switch
        {
            CaptureSourceKind.Window => RestoreWindowSource(rememberToken),
            CaptureSourceKind.Region => RestoreRegionSource(rememberToken),
            _ => RestoreDisplaySource(rememberToken)
        };

        if (restored is not null)
        {
            return restored;
        }

        return preferredKind switch
        {
            CaptureSourceKind.Window => GetWindowSources().FirstOrDefault()
                ?? GetDisplaySources().FirstOrDefault()
                ?? CaptureSourceDescriptor.CreateStub(CaptureSourceKind.Display),
            CaptureSourceKind.Region => GetDisplaySources().FirstOrDefault()
                ?? CaptureSourceDescriptor.CreateStub(CaptureSourceKind.Display),
            _ => GetDisplaySources().FirstOrDefault()
                ?? CaptureSourceDescriptor.CreateStub(CaptureSourceKind.Display)
        };
    }

    public CaptureSourcePreview Describe(CaptureSourceDescriptor source, RecordingOptions options) =>
        CaptureSourcePreviewCalculator.Describe(source, options);

    private static CaptureSourceDescriptor? SelectNextDisplaySource(CaptureSourceDescriptor? currentSource)
    {
        var displays = GetDisplaySources();
        return SelectNextSource(displays, currentSource);
    }

    private static CaptureSourceDescriptor? SelectNextWindowSource(CaptureSourceDescriptor? currentSource)
    {
        var windows = GetWindowSources();
        return SelectNextSource(windows, currentSource);
    }

    private static CaptureSourceDescriptor? SelectNextSource(
        IReadOnlyList<CaptureSourceDescriptor> sources,
        CaptureSourceDescriptor? currentSource)
    {
        if (sources.Count == 0)
        {
            return null;
        }

        if (currentSource is null)
        {
            return sources[0];
        }

        for (var index = 0; index < sources.Count; index++)
        {
            if (string.Equals(sources[index].RememberToken, currentSource.RememberToken, StringComparison.Ordinal))
            {
                return sources[(index + 1) % sources.Count];
            }
        }

        return sources[0];
    }

    private static CaptureSourceDescriptor? RestoreDisplaySource(string? rememberToken)
    {
        var displays = GetDisplaySources();
        if (displays.Count == 0)
        {
            return null;
        }

        if (string.IsNullOrWhiteSpace(rememberToken))
        {
            return displays.FirstOrDefault(display => display.DisplayName.Contains("Primary", StringComparison.Ordinal))
                ?? displays[0];
        }

        if (rememberToken.StartsWith("display:", StringComparison.Ordinal))
        {
            if (TryDecodeOpaquePayload(rememberToken, "display:", out var displayId))
            {
                return displays.FirstOrDefault(source => string.Equals(source.DisplayId, displayId, StringComparison.Ordinal));
            }

            if (TryParseLegacyDisplayIndex(rememberToken, out var displayIndex))
            {
                return displayIndex == 1
                    ? displays.FirstOrDefault(display => display.DisplayName.Contains("Primary", StringComparison.Ordinal)) ?? displays[0]
                    : displays.ElementAtOrDefault(displayIndex - 1) ?? displays[0];
            }
        }

        return null;
    }

    private static CaptureSourceDescriptor? RestoreWindowSource(string? rememberToken)
    {
        var windows = GetWindowSources();
        if (windows.Count == 0 || string.IsNullOrWhiteSpace(rememberToken))
        {
            return null;
        }

        if (!TryDecodeOpaquePayload(rememberToken, "window:", out var payload))
        {
            return null;
        }

        var parts = payload.Split('|', 2);
        if (parts.Length != 2)
        {
            return null;
        }

        var processName = parts[0];
        var title = parts[1];

        return windows.FirstOrDefault(source =>
                string.Equals(ReadWindowProcessName(source.WindowHandle), processName, StringComparison.OrdinalIgnoreCase) &&
                string.Equals(source.WindowTitle, title, StringComparison.Ordinal))
            ?? windows.FirstOrDefault(source => string.Equals(source.WindowTitle, title, StringComparison.Ordinal));
    }

    private static CaptureSourceDescriptor? RestoreRegionSource(string? rememberToken)
    {
        if (string.IsNullOrWhiteSpace(rememberToken) ||
            !rememberToken.StartsWith("region:", StringComparison.Ordinal))
        {
            return null;
        }

        var payload = rememberToken["region:".Length..];
        var parts = payload.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 4 ||
            !int.TryParse(parts[0], out var x) ||
            !int.TryParse(parts[1], out var y) ||
            !int.TryParse(parts[2], out var width) ||
            !int.TryParse(parts[3], out var height))
        {
            return null;
        }

        var region = new ScreenRegion(x, y, width, height)
            .ClipTo(CaptureSourcePreviewCalculator.VirtualDesktopBounds);

        return region.MeetsMinimumSize
            ? CreateRegionSource(region)
            : null;
    }

    private static IReadOnlyList<CaptureSourceDescriptor> GetDisplaySources()
    {
        var displays = new List<DisplayEntry>();
        var callback = new EnumDisplayMonitorsProc((nint monitorHandle, nint deviceContext, ref Rect monitorRect, nint monitorData) =>
        {
            _ = deviceContext;
            _ = monitorRect;
            _ = monitorData;
            var info = MonitorInfoEx.Create();
            if (!GetMonitorInfo(monitorHandle, ref info))
            {
                return true;
            }

            var bounds = new ScreenRegion(
                info.Monitor.Left,
                info.Monitor.Top,
                info.Monitor.Right - info.Monitor.Left,
                info.Monitor.Bottom - info.Monitor.Top);

            if (!bounds.MeetsMinimumSize)
            {
                return true;
            }

            displays.Add(new DisplayEntry(info.DeviceName, bounds, (info.Flags & MonitorInfoPrimaryFlag) != 0));
            return true;
        });

        EnumDisplayMonitors(nint.Zero, nint.Zero, callback, nint.Zero);

        return displays
            .OrderBy(entry => entry.Bounds.X)
            .ThenBy(entry => entry.Bounds.Y)
            .Select((entry, index) => CreateDisplaySource(entry, index + 1))
            .ToArray();
    }

    private static IReadOnlyList<CaptureSourceDescriptor> GetWindowSources()
    {
        var windows = new List<WindowEntry>();
        var shellWindow = GetShellWindow();
        var currentProcessId = Environment.ProcessId;
        var foregroundWindow = GetForegroundWindow();

        var callback = new EnumWindowsProc((windowHandle, _) =>
        {
            if (windowHandle == nint.Zero ||
                windowHandle == shellWindow ||
                windowHandle == foregroundWindow && currentProcessId == ReadWindowProcessId(windowHandle) ||
                !IsWindowVisible(windowHandle) ||
                IsIconic(windowHandle))
            {
                return true;
            }

            var windowTextLength = GetWindowTextLength(windowHandle);
            if (windowTextLength <= 0)
            {
                return true;
            }

            var titleBuilder = new StringBuilder(windowTextLength + 1);
            _ = GetWindowText(windowHandle, titleBuilder, titleBuilder.Capacity);
            var title = titleBuilder.ToString().Trim();
            if (string.IsNullOrWhiteSpace(title))
            {
                return true;
            }

            if (!GetWindowRect(windowHandle, out var rect))
            {
                return true;
            }

            var bounds = new ScreenRegion(rect.Left, rect.Top, rect.Right - rect.Left, rect.Bottom - rect.Top);
            if (!bounds.MeetsMinimumSize)
            {
                return true;
            }

            var exStyle = GetWindowLongPtr(windowHandle, GwlExStyle).ToInt64();
            if ((exStyle & WsExToolWindow) != 0)
            {
                return true;
            }

            if (TryIsWindowCloaked(windowHandle))
            {
                return true;
            }

            var processId = ReadWindowProcessId(windowHandle);
            if (processId == currentProcessId)
            {
                return true;
            }

            var processName = ReadWindowProcessName(processId);
            windows.Add(new WindowEntry(windowHandle, title, processName, bounds, windowHandle == foregroundWindow));
            return true;
        });

        EnumWindows(callback, nint.Zero);

        return windows
            .OrderByDescending(entry => entry.IsForeground)
            .ThenBy(entry => entry.Title, StringComparer.OrdinalIgnoreCase)
            .Select(CreateWindowSource)
            .ToArray();
    }

    private static CaptureSourceDescriptor CreateDisplaySource(DisplayEntry entry, int displayIndex)
    {
        var primarySuffix = entry.IsPrimary ? " Primary" : string.Empty;
        var displayName = $"Display {displayIndex}{primarySuffix}";

        return new CaptureSourceDescriptor(
            CaptureSourceKind.Display,
            displayName,
            DisplayId: entry.DeviceName,
            RememberToken: EncodeOpaquePayload("display:", entry.DeviceName),
            Bounds: entry.Bounds);
    }

    private static CaptureSourceDescriptor CreateWindowSource(WindowEntry entry)
    {
        var displayName = TrimWindowTitle(entry.Title);

        return new CaptureSourceDescriptor(
            CaptureSourceKind.Window,
            displayName,
            WindowTitle: entry.Title,
            WindowHandle: entry.Handle,
            RememberToken: EncodeOpaquePayload("window:", $"{entry.ProcessName}|{entry.Title}"),
            Bounds: entry.Bounds);
    }

    private static CaptureSourceDescriptor CreateRegionSource(ScreenRegion region) =>
        new(
            CaptureSourceKind.Region,
            $"Region {region.Width}x{region.Height}",
            Region: region,
            RememberToken: $"region:{region.X},{region.Y},{region.Width},{region.Height}",
            Bounds: region);

    private static bool TryParseLegacyDisplayIndex(string token, out int displayIndex)
    {
        displayIndex = 0;
        var suffix = token["display:".Length..];
        return int.TryParse(suffix, out displayIndex) && displayIndex > 0;
    }

    private static string TrimWindowTitle(string title) =>
        title.Length <= 48 ? title : $"{title[..45]}...";

    private static string EncodeOpaquePayload(string prefix, string payload) =>
        prefix + Convert.ToBase64String(Encoding.UTF8.GetBytes(payload));

    private static bool TryDecodeOpaquePayload(string token, string prefix, out string payload)
    {
        payload = string.Empty;
        if (!token.StartsWith(prefix, StringComparison.Ordinal))
        {
            return false;
        }

        try
        {
            payload = Encoding.UTF8.GetString(Convert.FromBase64String(token[prefix.Length..]));
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static int ReadWindowProcessId(nint windowHandle)
    {
        _ = GetWindowThreadProcessId(windowHandle, out var processId);
        return unchecked((int)processId);
    }

    private static string ReadWindowProcessName(nint windowHandle) =>
        ReadWindowProcessName(ReadWindowProcessId(windowHandle));

    private static string ReadWindowProcessName(int processId)
    {
        if (processId <= 0)
        {
            return string.Empty;
        }

        try
        {
            using var process = Process.GetProcessById(processId);
            return process.ProcessName;
        }
        catch
        {
            return string.Empty;
        }
    }

    private static bool TryIsWindowCloaked(nint windowHandle)
    {
        const int dwmCloakedAttribute = 14;
        if (DwmGetWindowAttribute(windowHandle, dwmCloakedAttribute, out int isCloaked, sizeof(int)) != 0)
        {
            return false;
        }

        return isCloaked != 0;
    }

    private sealed record DisplayEntry(string DeviceName, ScreenRegion Bounds, bool IsPrimary);

    private sealed record WindowEntry(
        nint Handle,
        string Title,
        string ProcessName,
        ScreenRegion Bounds,
        bool IsForeground);

    private delegate bool EnumWindowsProc(nint hWnd, nint lParam);

    private delegate bool EnumDisplayMonitorsProc(nint monitorHandle, nint hdc, ref Rect monitorRect, nint lParam);

    private const int MonitorInfoPrimaryFlag = 0x00000001;
    private const int GwlExStyle = -20;
    private const long WsExToolWindow = 0x00000080L;

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, nint lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumDisplayMonitors(
        nint hdc,
        nint clipRect,
        EnumDisplayMonitorsProc callback,
        nint data);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool GetMonitorInfo(nint hMonitor, ref MonitorInfoEx lpmi);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(nint hWnd);

    [DllImport("user32.dll")]
    private static extern bool IsIconic(nint hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLength(nint hWnd);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(nint hWnd, out Rect lpRect);

    [DllImport("user32.dll")]
    private static extern nint GetWindowLongPtr(nint hWnd, int nIndex);

    [DllImport("user32.dll")]
    private static extern nint GetShellWindow();

    [DllImport("user32.dll")]
    private static extern nint GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint hWnd, out uint lpdwProcessId);

    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(nint hwnd, int dwAttribute, out int pvAttribute, int cbAttribute);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct MonitorInfoEx
    {
        public int Size;
        public Rect Monitor;
        public Rect WorkArea;
        public int Flags;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string DeviceName;

        public static MonitorInfoEx Create() =>
            new()
            {
                Size = Marshal.SizeOf<MonitorInfoEx>(),
                DeviceName = string.Empty
            };
    }
}

internal static class CaptureSourcePreviewCalculator
{
    internal static ScreenRegion VirtualDesktopBounds =>
        new(
            GetSystemMetrics(SystemMetricVirtualScreenX),
            GetSystemMetrics(SystemMetricVirtualScreenY),
            GetSystemMetrics(SystemMetricVirtualScreenWidth),
            GetSystemMetrics(SystemMetricVirtualScreenHeight));

    internal static CaptureSourcePreview Describe(CaptureSourceDescriptor source, RecordingOptions options)
    {
        var virtualDesktop = VirtualDesktopBounds;
        var requestedRegion = ResolveRequestedRegion(source) ?? virtualDesktop;
        var effectiveRegion = requestedRegion.ClipTo(virtualDesktop);
        var wasClipped = effectiveRegion != requestedRegion;

        if (!effectiveRegion.MeetsMinimumSize)
        {
            effectiveRegion = virtualDesktop;
            wasClipped = true;
        }

        var sourceWidth = effectiveRegion.Width;
        var sourceHeight = effectiveRegion.Height;

        var rawTargetHeight = options.Resolution switch
        {
            ResolutionOption.P720 => Math.Min(sourceHeight, 720),
            ResolutionOption.P1080 => Math.Min(sourceHeight, 1080),
            _ => sourceHeight
        };

        var normalizedTargetHeight = NormalizeEvenDimension(rawTargetHeight);
        var rawTargetWidth = Math.Max(
            (int)((long)sourceWidth * normalizedTargetHeight / Math.Max(sourceHeight, 1)),
            ScreenRegion.MinimumDimension);
        var normalizedTargetWidth = NormalizeEvenDimension(rawTargetWidth);

        return new CaptureSourcePreview(
            effectiveRegion,
            normalizedTargetWidth,
            normalizedTargetHeight,
            wasClipped,
            WasScaledFromSource: normalizedTargetWidth != sourceWidth || normalizedTargetHeight != sourceHeight,
            WasAdjustedToEvenDimensions: normalizedTargetWidth != rawTargetWidth || normalizedTargetHeight != rawTargetHeight);
    }

    private static ScreenRegion? ResolveRequestedRegion(CaptureSourceDescriptor source)
    {
        if (source.Kind == CaptureSourceKind.Window &&
            source.WindowHandle != nint.Zero &&
            GetWindowRect(source.WindowHandle, out var windowRect))
        {
            var windowBounds = new ScreenRegion(
                windowRect.Left,
                windowRect.Top,
                windowRect.Right - windowRect.Left,
                windowRect.Bottom - windowRect.Top);
            if (windowBounds.MeetsMinimumSize)
            {
                return windowBounds;
            }
        }

        return source.Region ?? source.Bounds;
    }

    private static int NormalizeEvenDimension(int value)
    {
        if (value <= ScreenRegion.MinimumDimension)
        {
            return ScreenRegion.MinimumDimension;
        }

        return value % 2 == 0 ? value : value - 1;
    }

    private const int SystemMetricVirtualScreenX = 76;
    private const int SystemMetricVirtualScreenY = 77;
    private const int SystemMetricVirtualScreenWidth = 78;
    private const int SystemMetricVirtualScreenHeight = 79;

    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int nIndex);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(nint hWnd, out Rect lpRect);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
}
