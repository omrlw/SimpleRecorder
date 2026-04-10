using System.Runtime.InteropServices;

namespace SimpleRecorder.Infrastructure.Native;

internal static class NativeMethods
{
    internal const string LibraryName = "SimpleRecorder.Engine.Native";

    internal static bool TryLoad(out nint handle) => NativeLibrary.TryLoad(LibraryName, out handle);

    internal static void FreeIfLoaded(nint handle)
    {
        if (handle != nint.Zero)
        {
            NativeLibrary.Free(handle);
        }
    }
}
