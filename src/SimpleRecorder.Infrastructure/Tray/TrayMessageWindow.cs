using System.Runtime.InteropServices;

namespace SimpleRecorder.Infrastructure.Tray;

internal sealed class TrayMessageWindow : IDisposable
{
    private readonly string _className = $"SimpleRecorder.Tray.{Guid.NewGuid():N}";
    private readonly WndProc _wndProc;
    private ushort _classAtom;

    public TrayMessageWindow()
    {
        _wndProc = WindowProc;
        Register();
        Handle = CreateMessageWindow();
    }

    public delegate nint WndProc(nint hWnd, uint msg, nuint wParam, nint lParam);

    public event WndProc? MessageReceived;

    public nint Handle { get; }

    public void Dispose()
    {
        if (Handle != nint.Zero)
        {
            DestroyWindow(Handle);
        }

        if (_classAtom != 0)
        {
            UnregisterClass(_className, GetModuleHandle(null));
        }
    }

    private void Register()
    {
        var wc = new WNDCLASS
        {
            lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_wndProc),
            hInstance = GetModuleHandle(null),
            lpszClassName = _className
        };

        _classAtom = RegisterClass(ref wc);
        if (_classAtom == 0)
        {
            throw new InvalidOperationException("Unable to register tray message window class.");
        }
    }

    private nint CreateMessageWindow()
    {
        var handle = CreateWindowEx(
            0,
            _className,
            "SimpleRecorderTrayWindow",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nint.Zero,
            GetModuleHandle(null),
            nint.Zero);

        if (handle == nint.Zero)
        {
            throw new InvalidOperationException("Unable to create tray message window.");
        }

        return handle;
    }

    private nint WindowProc(nint hWnd, uint msg, nuint wParam, nint lParam) =>
        MessageReceived?.Invoke(hWnd, msg, wParam, lParam) ?? DefWindowProc(hWnd, msg, wParam, lParam);

    private const int HWND_MESSAGE = -3;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ushort RegisterClass(ref WNDCLASS lpWndClass);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool UnregisterClass(string lpClassName, nint hInstance);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateWindowEx(
        int dwExStyle,
        string lpClassName,
        string lpWindowName,
        int dwStyle,
        int x,
        int y,
        int nWidth,
        int nHeight,
        int hWndParent,
        nint hMenu,
        nint hInstance,
        nint lpParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(nint hWnd);

    [DllImport("user32.dll")]
    private static extern nint DefWindowProc(nint hWnd, uint msg, nuint wParam, nint lParam);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern nint GetModuleHandle(string? lpModuleName);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WNDCLASS
    {
        public uint style;
        public nint lpfnWndProc;
        public int cbClsExtra;
        public int cbWndExtra;
        public nint hInstance;
        public nint hIcon;
        public nint hCursor;
        public nint hbrBackground;
        public string? lpszMenuName;
        public string lpszClassName;
    }
}
