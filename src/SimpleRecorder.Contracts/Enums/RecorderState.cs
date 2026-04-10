namespace SimpleRecorder.Contracts.Enums;

public enum RecorderState
{
    Idle = 0,
    SourceSelected = 1,
    Countdown = 2,
    Recording = 3,
    Paused = 4,
    StoppingSaving = 5,
    ScreenshotSuccess = 6,
    ErrorNonBlocking = 7
}
