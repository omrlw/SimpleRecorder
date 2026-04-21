using System.Runtime.InteropServices;
using SimpleRecorder.Contracts.Models;

namespace SimpleRecorder.Infrastructure.SourceSelection;

internal static class RegionSelectionOverlay
{
    internal static Task<ScreenRegion?> PickAsync(ScreenRegion? initialRegion, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        var completionSource = new TaskCompletionSource<ScreenRegion?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() => RunSelectionLoop(initialRegion, cancellationToken, completionSource))
        {
            IsBackground = true,
            Name = "SimpleRecorder.RegionSelection"
        };

        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        return completionSource.Task;
    }

    private static void RunSelectionLoop(
        ScreenRegion? initialRegion,
        CancellationToken cancellationToken,
        TaskCompletionSource<ScreenRegion?> completionSource)
    {
        try
        {
            using var window = new SelectionWindow(initialRegion, completionSource);
            using var cancellationRegistration = cancellationToken.Register(window.RequestClose);
            window.Run();
        }
        catch (OperationCanceledException)
        {
            completionSource.TrySetCanceled(cancellationToken);
        }
        catch (Exception ex)
        {
            completionSource.TrySetException(ex);
        }
    }

    private sealed class SelectionWindow : IDisposable
    {
        private readonly TaskCompletionSource<ScreenRegion?> _completionSource;
        private readonly string _className = $"SimpleRecorder.RegionSelection.{Guid.NewGuid():N}";
        private readonly WndProc _wndProc;
        private readonly ScreenRegion _virtualBounds = CaptureSourcePreviewCalculator.VirtualDesktopBounds;
        private readonly ScreenRegion? _initialRegion;
        private ushort _classAtom;
        private nint _windowHandle;
        private nint _desktopSnapshotDc;
        private nint _desktopSnapshotBitmap;
        private nint _desktopSnapshotPreviousBitmap;
        private nint _blackDc;
        private nint _blackBitmap;
        private nint _blackPreviousBitmap;
        private nint _accentDc;
        private nint _accentBitmap;
        private nint _accentPreviousBitmap;
        private nint _framePen;
        private nint _fontHandle;
        private ScreenRegion? _activeSelection;
        private POINT _dragAnchor;
        private bool _isDragging;
        private bool _isDisposed;

        internal SelectionWindow(ScreenRegion? initialRegion, TaskCompletionSource<ScreenRegion?> completionSource)
        {
            _completionSource = completionSource;
            _initialRegion = initialRegion is { MeetsMinimumSize: true }
                ? initialRegion.ClipTo(_virtualBounds)
                : null;
            _activeSelection = _initialRegion is { MeetsMinimumSize: true } region ? region : null;
            _wndProc = WindowProc;

            RegisterWindowClass();
            InitializeResources();
            CreateOverlayWindow();
        }

        internal void Run()
        {
            if (_windowHandle == nint.Zero)
            {
                _completionSource.TrySetResult(null);
                return;
            }

            ShowWindow(_windowHandle, ShowWindowShow);
            UpdateWindow(_windowHandle);
            SetForegroundWindow(_windowHandle);

            while (GetMessage(out var message, nint.Zero, 0, 0) > 0)
            {
                TranslateMessage(ref message);
                DispatchMessage(ref message);
            }

            if (!_completionSource.Task.IsCompleted)
            {
                _completionSource.TrySetResult(_activeSelection);
            }
        }

        internal void RequestClose()
        {
            if (_windowHandle != nint.Zero)
            {
                PostMessage(_windowHandle, WmClose, 0, nint.Zero);
            }
        }

        public void Dispose()
        {
            if (_isDisposed)
            {
                return;
            }

            _isDisposed = true;

            if (_windowHandle != nint.Zero)
            {
                DestroyWindow(_windowHandle);
                _windowHandle = nint.Zero;
            }

            ReleaseSelectionResources();

            if (_classAtom != 0)
            {
                UnregisterClass(_className, GetModuleHandle(null));
                _classAtom = 0;
            }
        }

        private void RegisterWindowClass()
        {
            var windowClass = new WNDCLASS
            {
                hCursor = LoadCursor(nint.Zero, CrossCursorId),
                hInstance = GetModuleHandle(null),
                lpszClassName = _className,
                lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_wndProc),
                hbrBackground = nint.Zero
            };

            _classAtom = RegisterClass(ref windowClass);
            if (_classAtom == 0)
            {
                throw new InvalidOperationException("Unable to register the precision selection window class.");
            }
        }

        private void InitializeResources()
        {
            var screenDc = GetDC(nint.Zero);
            if (screenDc == nint.Zero)
            {
                throw new InvalidOperationException("Unable to access the desktop device context.");
            }

            try
            {
                _desktopSnapshotDc = CreateCompatibleDC(screenDc);
                _desktopSnapshotBitmap = CreateCompatibleBitmap(screenDc, _virtualBounds.Width, _virtualBounds.Height);
                _desktopSnapshotPreviousBitmap = SelectObject(_desktopSnapshotDc, _desktopSnapshotBitmap);
                _ = BitBlt(
                    _desktopSnapshotDc,
                    0,
                    0,
                    _virtualBounds.Width,
                    _virtualBounds.Height,
                    screenDc,
                    _virtualBounds.X,
                    _virtualBounds.Y,
                    Srccopy);

                _blackDc = CreateCompatibleDC(screenDc);
                _blackBitmap = CreateCompatibleBitmap(screenDc, 1, 1);
                _blackPreviousBitmap = SelectObject(_blackDc, _blackBitmap);
                FillSolidBitmap(_blackDc, 0x000000);

                _accentDc = CreateCompatibleDC(screenDc);
                _accentBitmap = CreateCompatibleBitmap(screenDc, 1, 1);
                _accentPreviousBitmap = SelectObject(_accentDc, _accentBitmap);
                FillSolidBitmap(_accentDc, 0x362CFB);

                _framePen = CreatePen(PsSolid, 2, 0x362CFB);
                _fontHandle = GetStockObject(DefaultGuiFont);
            }
            finally
            {
                ReleaseDC(nint.Zero, screenDc);
            }
        }

        private void CreateOverlayWindow()
        {
            _windowHandle = CreateWindowEx(
                WsExTopMost | WsExToolWindow,
                _className,
                "SimpleRecorder Precision Selection",
                WsPopup,
                _virtualBounds.X,
                _virtualBounds.Y,
                _virtualBounds.Width,
                _virtualBounds.Height,
                nint.Zero,
                nint.Zero,
                GetModuleHandle(null),
                nint.Zero);

            if (_windowHandle == nint.Zero)
            {
                throw new InvalidOperationException("Unable to create the precision selection window.");
            }
        }

        private void ReleaseSelectionResources()
        {
            if (_framePen != nint.Zero)
            {
                DeleteObject(_framePen);
                _framePen = nint.Zero;
            }

            ReleaseBitmapResources(ref _accentDc, ref _accentBitmap, ref _accentPreviousBitmap);
            ReleaseBitmapResources(ref _blackDc, ref _blackBitmap, ref _blackPreviousBitmap);
            ReleaseBitmapResources(ref _desktopSnapshotDc, ref _desktopSnapshotBitmap, ref _desktopSnapshotPreviousBitmap);
        }

        private static void ReleaseBitmapResources(ref nint dc, ref nint bitmap, ref nint previousBitmap)
        {
            if (dc != nint.Zero && previousBitmap != nint.Zero)
            {
                SelectObject(dc, previousBitmap);
                previousBitmap = nint.Zero;
            }

            if (bitmap != nint.Zero)
            {
                DeleteObject(bitmap);
                bitmap = nint.Zero;
            }

            if (dc != nint.Zero)
            {
                DeleteDC(dc);
                dc = nint.Zero;
            }
        }

        private static void FillSolidBitmap(nint dc, int colorRef)
        {
            var brush = CreateSolidBrush(colorRef);
            try
            {
                var rect = new RECT { Left = 0, Top = 0, Right = 1, Bottom = 1 };
                _ = FillRect(dc, ref rect, brush);
            }
            finally
            {
                if (brush != nint.Zero)
                {
                    DeleteObject(brush);
                }
            }
        }

        private nint WindowProc(nint hwnd, uint message, nuint wParam, nint lParam)
        {
            switch (message)
            {
                case WmKeyDown:
                    return HandleKeyDown(hwnd, (int)wParam);
                case WmLButtonDown:
                    return HandleLeftButtonDown(hwnd);
                case WmMouseMove:
                    return HandleMouseMove(hwnd);
                case WmLButtonUp:
                    return HandleLeftButtonUp(hwnd);
                case WmRButtonDown:
                case WmClose:
                    _activeSelection = null;
                    DestroyWindow(hwnd);
                    return 0;
                case WmPaint:
                    Paint(hwnd);
                    return 0;
                case WmDestroy:
                    PostQuitMessage(0);
                    return 0;
                case WmEraseBkgnd:
                    return 1;
            }

            return DefWindowProc(hwnd, message, wParam, lParam);
        }

        private nint HandleKeyDown(nint hwnd, int virtualKey)
        {
            if (virtualKey == VirtualKeyEscape)
            {
                _activeSelection = null;
                DestroyWindow(hwnd);
                return 0;
            }

            if (virtualKey == VirtualKeyReturn && _activeSelection is { MeetsMinimumSize: true })
            {
                DestroyWindow(hwnd);
                return 0;
            }

            return 0;
        }

        private nint HandleLeftButtonDown(nint hwnd)
        {
            if (!GetCursorPos(out _dragAnchor))
            {
                return 0;
            }

            _isDragging = true;
            _activeSelection = new ScreenRegion(_dragAnchor.X, _dragAnchor.Y, 0, 0);
            SetCapture(hwnd);
            InvalidateRect(hwnd, nint.Zero, false);
            return 0;
        }

        private nint HandleMouseMove(nint hwnd)
        {
            if (!_isDragging || !GetCursorPos(out var cursor))
            {
                return 0;
            }

            _activeSelection = ScreenRegion
                .FromPoints(_dragAnchor.X, _dragAnchor.Y, cursor.X, cursor.Y)
                .ClipTo(_virtualBounds);
            InvalidateRect(hwnd, nint.Zero, false);
            return 0;
        }

        private nint HandleLeftButtonUp(nint hwnd)
        {
            if (!_isDragging)
            {
                return 0;
            }

            _isDragging = false;
            ReleaseCapture();

            if (_activeSelection is { MeetsMinimumSize: true })
            {
                DestroyWindow(hwnd);
                return 0;
            }

            _activeSelection = _initialRegion;
            InvalidateRect(hwnd, nint.Zero, false);
            return 0;
        }

        private void Paint(nint hwnd)
        {
            BeginPaint(hwnd, out var paintStruct);
            try
            {
                if (_desktopSnapshotDc == nint.Zero)
                {
                    return;
                }

                _ = BitBlt(
                    paintStruct.hdc,
                    0,
                    0,
                    _virtualBounds.Width,
                    _virtualBounds.Height,
                    _desktopSnapshotDc,
                    0,
                    0,
                    Srccopy);

                _ = AlphaBlend(
                    paintStruct.hdc,
                    0,
                    0,
                    _virtualBounds.Width,
                    _virtualBounds.Height,
                    _blackDc,
                    0,
                    0,
                    1,
                    1,
                    new BLENDFUNCTION(AcSrcOver, 0, 112, 0));

                if (_activeSelection is { MeetsMinimumSize: true } selection)
                {
                    var clientRect = ToClientRect(selection);
                    _ = BitBlt(
                        paintStruct.hdc,
                        clientRect.Left,
                        clientRect.Top,
                        selection.Width,
                        selection.Height,
                        _desktopSnapshotDc,
                        clientRect.Left,
                        clientRect.Top,
                        Srccopy);

                    _ = AlphaBlend(
                        paintStruct.hdc,
                        clientRect.Left,
                        clientRect.Top,
                        selection.Width,
                        selection.Height,
                        _accentDc,
                        0,
                        0,
                        1,
                        1,
                        new BLENDFUNCTION(AcSrcOver, 0, 28, 0));

                    var previousPen = SelectObject(paintStruct.hdc, _framePen);
                    var previousBrush = SelectObject(paintStruct.hdc, GetStockObject(NullBrush));
                    _ = Rectangle(
                        paintStruct.hdc,
                        clientRect.Left,
                        clientRect.Top,
                        clientRect.Right,
                        clientRect.Bottom);
                    SelectObject(paintStruct.hdc, previousBrush);
                    SelectObject(paintStruct.hdc, previousPen);
                }

                DrawTextOverlay(paintStruct.hdc);
            }
            finally
            {
                EndPaint(hwnd, ref paintStruct);
            }
        }

        private void DrawTextOverlay(nint dc)
        {
            var previousFont = SelectObject(dc, _fontHandle);
            _ = SetBkMode(dc, TransparentBackground);
            _ = SetTextColor(dc, 0xF4F4F4);

            DrawTextLine(dc, 24, 24, "Drag to select a region. Enter confirms. Esc cancels.");

            var selectionText = _activeSelection is { MeetsMinimumSize: true } selection
                ? $"Region {selection.Width}x{selection.Height} @ ({selection.X}, {selection.Y})"
                : $"Select at least {ScreenRegion.MinimumDimension}x{ScreenRegion.MinimumDimension} px";
            DrawTextLine(dc, 24, 52, selectionText);

            SelectObject(dc, previousFont);
        }

        private static void DrawTextLine(nint dc, int x, int y, string text) =>
            _ = TextOut(dc, x, y, text, text.Length);

        private RECT ToClientRect(ScreenRegion region) =>
            new()
            {
                Left = region.X - _virtualBounds.X,
                Top = region.Y - _virtualBounds.Y,
                Right = region.Right - _virtualBounds.X,
                Bottom = region.Bottom - _virtualBounds.Y
            };

        private delegate nint WndProc(nint hWnd, uint msg, nuint wParam, nint lParam);

        private const uint WmClose = 0x0010;
        private const uint WmDestroy = 0x0002;
        private const uint WmPaint = 0x000F;
        private const uint WmEraseBkgnd = 0x0014;
        private const uint WmKeyDown = 0x0100;
        private const uint WmMouseMove = 0x0200;
        private const uint WmLButtonDown = 0x0201;
        private const uint WmLButtonUp = 0x0202;
        private const uint WmRButtonDown = 0x0204;
        private const int ShowWindowShow = 5;
        private const int VirtualKeyEscape = 0x1B;
        private const int VirtualKeyReturn = 0x0D;
        private const int TransparentBackground = 1;
        private const int CrossCursorId = 32515;
        private const int DefaultGuiFont = 17;
        private const int NullBrush = 5;
        private const int PsSolid = 0;
        private const int WsPopup = unchecked((int)0x80000000);
        private const int WsExTopMost = 0x00000008;
        private const int WsExToolWindow = 0x00000080;
        private const int Srccopy = 0x00CC0020;
        private const byte AcSrcOver = 0;

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
            nint hWndParent,
            nint hMenu,
            nint hInstance,
            nint lpParam);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool DestroyWindow(nint hWnd);

        [DllImport("user32.dll")]
        private static extern bool ShowWindow(nint hWnd, int nCmdShow);

        [DllImport("user32.dll")]
        private static extern bool UpdateWindow(nint hWnd);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(nint hWnd);

        [DllImport("user32.dll")]
        private static extern sbyte GetMessage(out MSG lpMsg, nint hWnd, uint wMsgFilterMin, uint wMsgFilterMax);

        [DllImport("user32.dll")]
        private static extern bool TranslateMessage(ref MSG lpMsg);

        [DllImport("user32.dll")]
        private static extern nint DispatchMessage(ref MSG lpMsg);

        [DllImport("user32.dll")]
        private static extern void PostQuitMessage(int nExitCode);

        [DllImport("user32.dll")]
        private static extern bool PostMessage(nint hWnd, uint msg, nuint wParam, nint lParam);

        [DllImport("user32.dll")]
        private static extern nint DefWindowProc(nint hWnd, uint msg, nuint wParam, nint lParam);

        [DllImport("user32.dll")]
        private static extern nint BeginPaint(nint hWnd, out PAINTSTRUCT lpPaint);

        [DllImport("user32.dll")]
        private static extern bool EndPaint(nint hWnd, ref PAINTSTRUCT lpPaint);

        [DllImport("user32.dll")]
        private static extern bool InvalidateRect(nint hWnd, nint lpRect, bool bErase);

        [DllImport("user32.dll")]
        private static extern nint LoadCursor(nint hInstance, int lpCursorName);

        [DllImport("user32.dll")]
        private static extern bool GetCursorPos(out POINT lpPoint);

        [DllImport("user32.dll")]
        private static extern nint SetCapture(nint hWnd);

        [DllImport("user32.dll")]
        private static extern bool ReleaseCapture();

        [DllImport("user32.dll")]
        private static extern nint GetDC(nint hWnd);

        [DllImport("user32.dll")]
        private static extern int ReleaseDC(nint hWnd, nint hdc);

        [DllImport("gdi32.dll")]
        private static extern nint CreateCompatibleDC(nint hdc);

        [DllImport("gdi32.dll")]
        private static extern bool DeleteDC(nint hdc);

        [DllImport("gdi32.dll")]
        private static extern nint CreateCompatibleBitmap(nint hdc, int cx, int cy);

        [DllImport("gdi32.dll")]
        private static extern nint SelectObject(nint hdc, nint h);

        [DllImport("gdi32.dll")]
        private static extern bool DeleteObject(nint ho);

        [DllImport("gdi32.dll")]
        private static extern bool BitBlt(
            nint hdc,
            int x,
            int y,
            int cx,
            int cy,
            nint hdcSrc,
            int x1,
            int y1,
            int rop);

        [DllImport("gdi32.dll")]
        private static extern int SetBkMode(nint hdc, int mode);

        [DllImport("gdi32.dll")]
        private static extern int SetTextColor(nint hdc, int color);

        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)]
        private static extern bool TextOut(nint hdc, int x, int y, string lpString, int c);

        [DllImport("gdi32.dll")]
        private static extern nint CreateSolidBrush(int colorRef);

        [DllImport("user32.dll")]
        private static extern int FillRect(nint hDC, ref RECT lprc, nint hbr);

        [DllImport("gdi32.dll")]
        private static extern nint CreatePen(int fnPenStyle, int nWidth, int crColor);

        [DllImport("gdi32.dll")]
        private static extern bool Rectangle(nint hdc, int left, int top, int right, int bottom);

        [DllImport("gdi32.dll")]
        private static extern nint GetStockObject(int i);

        [DllImport("msimg32.dll", EntryPoint = "AlphaBlend")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool AlphaBlend(
            nint hdcDest,
            int xoriginDest,
            int yoriginDest,
            int wDest,
            int hDest,
            nint hdcSrc,
            int xoriginSrc,
            int yoriginSrc,
            int wSrc,
            int hSrc,
            BLENDFUNCTION ftn);

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

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT
        {
            public int X;
            public int Y;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MSG
        {
            public nint hwnd;
            public uint message;
            public nuint wParam;
            public nint lParam;
            public uint time;
            public POINT pt;
            public uint lPrivate;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PAINTSTRUCT
        {
            public nint hdc;
            public bool fErase;
            public RECT rcPaint;
            public bool fRestore;
            public bool fIncUpdate;

            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
            public byte[] rgbReserved;
        }

        [StructLayout(LayoutKind.Sequential)]
        private readonly struct BLENDFUNCTION
        {
            public readonly byte BlendOp;
            public readonly byte BlendFlags;
            public readonly byte SourceConstantAlpha;
            public readonly byte AlphaFormat;

            public BLENDFUNCTION(byte blendOp, byte blendFlags, byte sourceConstantAlpha, byte alphaFormat)
            {
                BlendOp = blendOp;
                BlendFlags = blendFlags;
                SourceConstantAlpha = sourceConstantAlpha;
                AlphaFormat = alphaFormat;
            }
        }
    }
}
