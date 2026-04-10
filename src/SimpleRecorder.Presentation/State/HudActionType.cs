namespace SimpleRecorder.Presentation.State;

public enum HudActionType
{
    SelectSource = 0,
    RequestStart = 1,
    CountdownCompleted = 2,
    CountdownCancelled = 3,
    RequestPause = 4,
    RequestResume = 5,
    RequestStop = 6,
    StopCompleted = 7,
    ErrorRaised = 8
}
