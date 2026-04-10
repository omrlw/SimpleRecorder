namespace SimpleRecorder.Presentation.State;

public enum HudSessionState
{
    Idle = 0,
    SourceSelected = 1,
    Countdown = 2,
    Recording = 3,
    Paused = 4,
    StoppingSaving = 5
}
