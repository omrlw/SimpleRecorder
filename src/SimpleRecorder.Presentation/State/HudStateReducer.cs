namespace SimpleRecorder.Presentation.State;

public static class HudStateReducer
{
    public static HudSessionState Reduce(HudSessionState current, HudActionType action) =>
        (current, action) switch
        {
            (_, HudActionType.SelectSource) => HudSessionState.SourceSelected,
            (HudSessionState.SourceSelected, HudActionType.RequestStart) => HudSessionState.Countdown,
            (HudSessionState.Countdown, HudActionType.CountdownCompleted) => HudSessionState.Recording,
            (HudSessionState.Countdown, HudActionType.CountdownCancelled) => HudSessionState.SourceSelected,
            (HudSessionState.Recording, HudActionType.RequestPause) => HudSessionState.Paused,
            (HudSessionState.Paused, HudActionType.RequestResume) => HudSessionState.Recording,
            (HudSessionState.Recording, HudActionType.RequestStop) => HudSessionState.StoppingSaving,
            (HudSessionState.Paused, HudActionType.RequestStop) => HudSessionState.StoppingSaving,
            (HudSessionState.StoppingSaving, HudActionType.StopCompleted) => HudSessionState.SourceSelected,
            (_, HudActionType.ErrorRaised) => current,
            _ => current
        };
}
