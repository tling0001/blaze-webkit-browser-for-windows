namespace WebKitBrowser.Models;

public sealed class BrowserSettings
{
    public bool TabBarAboveToolbar { get; set; } = true;
    public bool JavaScriptEnabled { get; set; } = true;
    public bool LoadImagesAutomatically { get; set; } = true;
    public string HomePage { get; set; } = "https://www.google.com";
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
