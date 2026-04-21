using System.Runtime.InteropServices;
using SimpleRecorder.Contracts.Enums;
using SimpleRecorder.Contracts.Models;
using SimpleRecorder.Contracts.Services;

namespace SimpleRecorder.Infrastructure.Tray;

public sealed class TrayIconService : ITrayService
{
    private const uint CallbackMessage = 0x8001;
    private const uint WM_COMMAND = 0x0111;
    private const uint WM_CONTEXTMENU = 0x007B;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_RBUTTONUP = 0x0205;
    private const uint WM_NULL = 0x0000;
    private const uint NIM_ADD = 0x00000000;
    private const uint NIM_MODIFY = 0x00000001;
    private const uint NIM_DELETE = 0x00000002;
    private const uint NIM_SETVERSION = 0x00000004;
    private const uint NIF_MESSAGE = 0x00000001;
    private const uint NIF_ICON = 0x00000002;
    private const uint NIF_TIP = 0x00000004;
    private const uint NOTIFYICON_VERSION_4 = 4;
    private const uint MF_STRING = 0x00000000;
    private const uint MF_SEPARATOR = 0x00000800;
    private const uint MF_GRAYED = 0x00000001;
    private const uint TPM_LEFTALIGN = 0x0000;
    private const uint TPM_BOTTOMALIGN = 0x0020;
    private const uint TPM_RIGHTBUTTON = 0x0002;

    private const uint CommandShowHud = 1001;
    private const uint CommandToggleRecording = 1002;
    private const uint CommandTogglePause = 1003;
    private const uint CommandExit = 1004;

    private readonly Guid _trayGuid = new("5F69B111-66E1-4D3D-BCE1-56A1C0A5937D");
    private readonly TrayMessageWindow _messageWindow = new();
    private RecorderStatusSnapshot _snapshot = new(RecorderState.Idle, null, Message: "SimpleRecorder");
    private bool _initialized;

    public TrayIconService()
    {
        _messageWindow.MessageReceived += HandleWindowMessage;
    }

    public event EventHandler<TrayCommand>? CommandInvoked;

    public void Initialize()
    {
        if (_initialized)
        {
            return;
        }

        var data = CreateData();
        Shell_NotifyIcon(NIM_ADD, ref data);
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon(NIM_SETVERSION, ref data);
        _initialized = true;
    }

    public void Update(RecorderStatusSnapshot snapshot)
    {
        _snapshot = snapshot;
        if (!_initialized)
        {
            return;
        }

        var data = CreateData();
        Shell_NotifyIcon(NIM_MODIFY, ref data);
    }

    public void Dispose()
    {
        if (_initialized)
        {
            var data = CreateData();
            Shell_NotifyIcon(NIM_DELETE, ref data);
        }

        _messageWindow.Dispose();
    }

    private nint HandleWindowMessage(nint hWnd, uint msg, nuint wParam, nint lParam)
    {
        if (msg == CallbackMessage)
        {
            var mouseMessage = (uint)lParam.ToInt64();
            if (mouseMessage == WM_LBUTTONUP)
            {
                CommandInvoked?.Invoke(this, TrayCommand.ShowHud);
            }
            else if (mouseMessage == WM_RBUTTONUP || mouseMessage == WM_CONTEXTMENU)
            {
                ShowContextMenu(hWnd);
            }

            return 0;
        }

        if (msg == WM_COMMAND)
        {
            var commandId = (uint)(wParam & 0xFFFF);
            switch (commandId)
            {
                case CommandShowHud:
                    CommandInvoked?.Invoke(this, TrayCommand.ShowHud);
                    break;
                case CommandToggleRecording:
                    CommandInvoked?.Invoke(this, TrayCommand.ToggleRecording);
                    break;
                case CommandTogglePause:
                    CommandInvoked?.Invoke(this, TrayCommand.TogglePause);
                    break;
                case CommandExit:
                    CommandInvoked?.Invoke(this, TrayCommand.Exit);
                    break;
            }

            return 0;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    private void ShowContextMenu(nint windowHandle)
    {
        var menu = CreatePopupMenu();
        try
        {
            AppendMenu(menu, MF_STRING, CommandShowHud, "Show HUD");
            AppendMenu(menu, MF_SEPARATOR, 0, string.Empty);
            AppendMenu(menu, MF_STRING, CommandToggleRecording, _snapshot.State is RecorderState.Recording or RecorderState.Paused ? "Stop recording" : "Start recording");

            var pauseFlags = _snapshot.State is RecorderState.Recording or RecorderState.Paused ? MF_STRING : MF_STRING | MF_GRAYED;
            AppendMenu(menu, pauseFlags, CommandTogglePause, _snapshot.State == RecorderState.Paused ? "Resume" : "Pause");
            AppendMenu(menu, MF_SEPARATOR, 0, string.Empty);
            AppendMenu(menu, MF_STRING, CommandExit, "Quit");

            GetCursorPos(out var point);
            SetForegroundWindow(windowHandle);
            TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, point.X, point.Y, windowHandle, nint.Zero);
            PostMessage(windowHandle, WM_NULL, 0, 0);
        }
        finally
        {
            DestroyMenu(menu);
        }
    }

    private NOTIFYICONDATA CreateData() =>
        new()
        {
            cbSize = (uint)Marshal.SizeOf<NOTIFYICONDATA>(),
            hWnd = _messageWindow.Handle,
            uID = 1,
            uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
            uCallbackMessage = CallbackMessage,
            hIcon = LoadIcon(nint.Zero, (nint)0x7F00),
            szTip = BuildTooltip(),
            guidItem = _trayGuid
        };

    private string BuildTooltip()
    {
        var stateLabel = _snapshot.State switch
        {
            RecorderState.Recording => "Recording",
            RecorderState.Paused => "Paused",
            RecorderState.StoppingSaving => "Saving",
            _ => "Ready"
        };

        return $"SimpleRecorder - {stateLabel}";
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern bool Shell_NotifyIcon(uint dwMessage, ref NOTIFYICONDATA lpData);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint LoadIcon(nint hInstance, nint lpIconName);

    [DllImport("user32.dll")]
    private static extern nint CreatePopupMenu();

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool AppendMenu(nint hMenu, uint uFlags, uint uIDNewItem, string lpNewItem);

    [DllImport("user32.dll")]
    private static extern bool DestroyMenu(nint hMenu);

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(nint hWnd);

    [DllImport("user32.dll")]
    private static extern bool TrackPopupMenuEx(nint hMenu, uint uFlags, int x, int y, nint hWnd, nint lptpm);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(nint hWnd, uint msg, nuint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern nint DefWindowProc(nint hWnd, uint msg, nuint wParam, nint lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NOTIFYICONDATA
    {
        public uint cbSize;
        public nint hWnd;
        public uint uID;
        public uint uFlags;
        public uint uCallbackMessage;
        public nint hIcon;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string szTip;

        public uint dwState;
        public uint dwStateMask;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string szInfo;

        public uint uVersion;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string szInfoTitle;

        public uint dwInfoFlags;
        public Guid guidItem;
        public nint hBalloonIcon;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int X;
        public int Y;
    }
}
