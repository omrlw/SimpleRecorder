namespace SimpleRecorder.Contracts.Models;

public sealed record ScreenRegion(int X, int Y, int Width, int Height)
{
    public const int MinimumDimension = 2;

    public int Right => X + Width;

    public int Bottom => Y + Height;

    public bool HasArea => Width > 0 && Height > 0;

    public bool MeetsMinimumSize => Width >= MinimumDimension && Height >= MinimumDimension;

    public static ScreenRegion FromEdges(int left, int top, int right, int bottom) =>
        new(left, top, Math.Max(0, right - left), Math.Max(0, bottom - top));

    public static ScreenRegion FromPoints(int startX, int startY, int endX, int endY) =>
        FromEdges(
            Math.Min(startX, endX),
            Math.Min(startY, endY),
            Math.Max(startX, endX),
            Math.Max(startY, endY));

    public ScreenRegion ClipTo(ScreenRegion bounds)
    {
        var clippedLeft = Math.Max(X, bounds.X);
        var clippedTop = Math.Max(Y, bounds.Y);
        var clippedRight = Math.Min(Right, bounds.Right);
        var clippedBottom = Math.Min(Bottom, bounds.Bottom);
        return FromEdges(clippedLeft, clippedTop, clippedRight, clippedBottom);
    }

    public override string ToString() => $"{Width}x{Height} @ ({X}, {Y})";
}
