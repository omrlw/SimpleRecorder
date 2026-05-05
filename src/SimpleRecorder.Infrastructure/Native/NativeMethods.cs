using System.Runtime.InteropServices;

namespace SimpleRecorder.Infrastructure.Native;

internal static partial class NativeMethods
{
    internal const string LibraryName = "SimpleRecorder.Engine.Native";
    internal const int CurrentAbiVersion = 3;
    internal const int CurrentStructVersion = 2;

    internal enum SrResultCode
    {
        Ok = 0,
        InvalidArgument = 1,
        InvalidState = 2,
        NotInitialized = 3
    }

    internal enum SrRecorderState
    {
        Idle = 0,
        SourceSelected = 1,
        Countdown = 2,
        Recording = 3,
        Paused = 4,
        StoppingSaving = 5,
        ErrorNonBlocking = 7
    }

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void SrStatusCallback(nint context, SrStatusEvent status);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct SrRect
    {
        internal readonly int X;
        internal readonly int Y;
        internal readonly int Width;
        internal readonly int Height;

        internal SrRect(int x, int y, int width, int height)
        {
            X = x;
            Y = y;
            Width = width;
            Height = height;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct SrCaptureSource
    {
        internal readonly int Version;
        internal readonly int Kind;
        internal readonly long WindowHandle;
        internal readonly SrRect Region;

        internal SrCaptureSource(int version, int kind, long windowHandle, SrRect region)
        {
            Version = version;
            Kind = kind;
            WindowHandle = windowHandle;
            Region = region;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct SrRecordingOptions
    {
        internal readonly int Version;
        internal readonly int FrameRate;
        internal readonly int Resolution;
        internal readonly int QualityPreset;
        internal readonly int CountdownSeconds;
        internal readonly int IncludeSystemAudio;
        internal readonly int IncludeMicrophone;
        internal readonly int EncoderPreference;
        internal readonly int VideoCodec;

        internal SrRecordingOptions(
            int version,
            int frameRate,
            int resolution,
            int qualityPreset,
            int countdownSeconds,
            int includeSystemAudio,
            int includeMicrophone,
            int encoderPreference,
            int videoCodec)
        {
            Version = version;
            FrameRate = frameRate;
            Resolution = resolution;
            QualityPreset = qualityPreset;
            CountdownSeconds = countdownSeconds;
            IncludeSystemAudio = includeSystemAudio;
            IncludeMicrophone = includeMicrophone;
            EncoderPreference = encoderPreference;
            VideoCodec = videoCodec;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct SrStatusEvent
    {
        internal readonly int Version;
        internal readonly int State;
        internal readonly int ActiveSourceKind;
        internal readonly int CountdownRemainingSeconds;
        internal readonly long TimestampUnixMilliseconds;

        internal SrStatusEvent(int version, int state, int activeSourceKind, int countdownRemainingSeconds, long timestampUnixMilliseconds)
        {
            Version = version;
            State = state;
            ActiveSourceKind = activeSourceKind;
            CountdownRemainingSeconds = countdownRemainingSeconds;
            TimestampUnixMilliseconds = timestampUnixMilliseconds;
        }
    }

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_get_abi_version")]
    internal static partial int GetAbiVersion();

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_create")]
    internal static partial nint Create();

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_destroy")]
    internal static partial void Destroy(nint engine);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_set_callback")]
    internal static partial int SetCallback(nint engine, nint callback, nint context);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_initialize")]
    internal static partial int Initialize(nint engine);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_prepare_recording_output", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int PrepareRecordingOutput(nint engine, string outputPath);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_start")]
    internal static partial int Start(nint engine, in SrCaptureSource source, in SrRecordingOptions options);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_pause")]
    internal static partial int Pause(nint engine);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_resume")]
    internal static partial int Resume(nint engine);

    [LibraryImport(LibraryName, EntryPoint = "sr_engine_stop")]
    internal static partial int Stop(nint engine);
}
