namespace WebKitBrowser.Models;

public enum BrowserSearchEngine
{
    Google,
    Bing,
    DuckDuckGo,
    Brave
}

public enum PopupHandlingMode
{
    Ask,
    AlwaysAllow,
    AlwaysDeny
}

public sealed class BrowserSettings
{
    public const string NativeStartPageUrl = "blaze://home";
    public bool TabBarAboveToolbar { get; set; } = true;
    public bool JavaScriptEnabled { get; set; } = true;
    public bool LoadImagesAutomatically { get; set; } = true;
    public bool ReduceMotion { get; set; } = false;
    public PopupHandlingMode PopupHandlingMode { get; set; } = PopupHandlingMode.Ask;
    public string HomePage { get; set; } = NativeStartPageUrl;
    public bool HomePageCustomized { get; set; }
    public BrowserSearchEngine SearchEngine { get; set; } = BrowserSearchEngine.Google;
}

public sealed class BookmarkEntry
{
    public string Title { get; set; } = string.Empty;
    public string Url { get; set; } = string.Empty;
    public DateTimeOffset AddedAt { get; set; } = DateTimeOffset.Now;
}

public sealed class HistoryEntry
{
    public string Title { get; set; } = string.Empty;
    public string Url { get; set; } = string.Empty;
    public DateTimeOffset VisitedAt { get; set; } = DateTimeOffset.Now;
}

public sealed class SessionState
{
    public List<string> TabUrls { get; set; } = [];
    public int SelectedIndex { get; set; }
}

public sealed class DownloadEntry
{
    public string FilePath { get; set; } = string.Empty;
    public string Status { get; set; } = "Starting";
    public long BytesReceived { get; set; }
    public long BytesExpected { get; set; }
    public DateTimeOffset UpdatedAt { get; set; } = DateTimeOffset.Now;

    public string FileName => Path.GetFileName(FilePath);
    public string ProgressText => BytesExpected > 0
        ? $"{Status} — {BytesReceived:N0} / {BytesExpected:N0} bytes"
        : Status;
}

public sealed class SitePermissionEntry
{
    public string Origin { get; set; } = string.Empty;
    public bool GeolocationAllowed { get; set; }
    public bool NotificationsAllowed { get; set; }
    public DateTimeOffset LastRequestedAt { get; set; } = DateTimeOffset.Now;
}

public sealed record StartPageShortcut(string Title, string Url, string Glyph, string AutomationId);
