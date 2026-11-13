using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using WebKitBrowser.Models;

namespace WebKitBrowser.ViewModels;

/// <summary>
/// Browser state and persistence. HWND placement and WinUI dialogs stay in
/// the view; navigation state and user data live here.
/// </summary>
public partial class MainPageViewModel : ObservableObject
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    private readonly ConcurrentDictionary<string, SitePermissionEntry> _permissionIndex = new(StringComparer.OrdinalIgnoreCase);
    private readonly SemaphoreSlim _saveGate = new(1, 1);
    private readonly string _dataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Blaze");

    public ObservableCollection<BrowserTab> Tabs { get; } = [];
    public ObservableCollection<BookmarkEntry> Bookmarks { get; } = [];
    public ObservableCollection<HistoryEntry> History { get; } = [];
    public ObservableCollection<DownloadEntry> Downloads { get; } = [];
    public ObservableCollection<SitePermissionEntry> SitePermissions { get; } = [];

    public BrowserSettings Settings { get; private set; } = new();

    [ObservableProperty]
    public partial BrowserTab? SelectedTab { get; set; }

    [ObservableProperty]
    public partial string AddressText { get; set; }

    [ObservableProperty]
    public partial string StatusMessage { get; set; }

    public MainPageViewModel()
    {
        AddressText = string.Empty;
        StatusMessage = string.Empty;
    }

    partial void OnSelectedTabChanged(BrowserTab? value)
    {
        AddressText = value?.Url ?? string.Empty;
    }

    public async Task InitializeAsync()
    {
        Directory.CreateDirectory(_dataDirectory);
        Settings = await ReadAsync<BrowserSettings>("settings.json") ?? new BrowserSettings();
        // Migrate the previous default to the native start page, while retaining any
        // custom homepage saved by an earlier Blaze version.
        if (!Settings.HomePageCustomized)
        {
            if (string.Equals(Settings.HomePage, "https://www.google.com", StringComparison.OrdinalIgnoreCase))
                Settings.HomePage = BrowserSettings.NativeStartPageUrl;
            else if (!string.Equals(Settings.HomePage, BrowserSettings.NativeStartPageUrl, StringComparison.OrdinalIgnoreCase))
                Settings.HomePageCustomized = true;
        }

        var savedBookmarks = await ReadAsync<List<BookmarkEntry>>("bookmarks.json") ?? [];
        var savedHistory = await ReadAsync<List<HistoryEntry>>("history.json") ?? [];
        var savedDownloads = await ReadAsync<List<DownloadEntry>>("downloads.json") ?? [];
        var savedPermissions = await ReadAsync<List<SitePermissionEntry>>("permissions.json") ?? [];
        foreach (var bookmark in savedBookmarks)
            Bookmarks.Add(bookmark);
        foreach (var historyItem in savedHistory)
            History.Add(historyItem);
        foreach (var download in savedDownloads)
        {
            if (download.Status is "Starting" or "Downloading")
                download.Status = "Interrupted";
            Downloads.Add(download);
        }
        foreach (var permission in savedPermissions)
        {
            SitePermissions.Add(permission);
            _permissionIndex[permission.Origin] = permission;
        }

        var session = Settings.StartupBehavior == StartupBehavior.ContinueSession
            ? await ReadAsync<SessionState>("session.json")
            : null;
        if (session?.TabUrls is { Count: > 0 })
        {
            var migrateLegacySingleGoogleTab = !Settings.HomePageCustomized
                && session.TabUrls.Count == 1
                && string.Equals(session.TabUrls[0], "https://www.google.com", StringComparison.OrdinalIgnoreCase);
            foreach (var url in session.TabUrls)
            {
                var restoredUrl = migrateLegacySingleGoogleTab ? BrowserSettings.NativeStartPageUrl : url;
                var tab = new BrowserTab { Url = restoredUrl };
                if (!tab.IsNativeStartPage)
                    tab.Title = restoredUrl;
                Tabs.Add(tab);
            }
            var selectedIndex = Math.Clamp(session.SelectedIndex, 0, Tabs.Count - 1);
            SelectedTab = Tabs[selectedIndex];
        }
        else
        {
            AddTab(Settings.HomePage);
        }
    }

    public BrowserTab AddTab(string? initialUrl = null)
    {
        var tab = new BrowserTab { Url = initialUrl ?? Settings.HomePage };
        Tabs.Add(tab);
        SelectedTab = tab;
        return tab;
    }

    public void CloseTab(BrowserTab tab)
    {
        var index = Tabs.IndexOf(tab);
        if (index < 0)
            return;

        Tabs.RemoveAt(index);
        if (ReferenceEquals(SelectedTab, tab))
            SelectedTab = Tabs.Count > 0 ? Tabs[Math.Min(index, Tabs.Count - 1)] : null;
    }

    [RelayCommand]
    private void NavigateAddress(string? address)
    {
        if (SelectedTab is null || string.IsNullOrWhiteSpace(address))
            return;
        var url = NormalizeAddress(address.Trim(), Settings);
        SelectedTab.Navigate(url);
        AddressText = url;
    }

    [RelayCommand]
    private void GoBack()
    {
        SelectedTab?.GoBack();
    }

    [RelayCommand]
    private void GoForward()
    {
        SelectedTab?.GoForward();
    }

    [RelayCommand]
    private void Reload()
    {
        SelectedTab?.Reload();
    }

    [RelayCommand]
    private void Stop()
    {
        SelectedTab?.Stop();
    }

    public void RecordNavigation(BrowserTab tab)
    {
        if (string.IsNullOrWhiteSpace(tab.Url) || tab.Url == "about:blank" ||
            !Uri.TryCreate(tab.Url, UriKind.Absolute, out var uri) ||
            (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
            return;

        tab.LastRecordedUrl = tab.Url;
        History.Insert(0, new HistoryEntry
        {
            Title = tab.Title,
            Url = tab.Url,
            VisitedAt = DateTimeOffset.Now
        });
        while (History.Count > 500)
            History.RemoveAt(History.Count - 1);
    }

    public bool ToggleBookmark(BrowserTab? tab)
    {
        if (tab is null || string.IsNullOrWhiteSpace(tab.Url) || tab.Url == "about:blank" || tab.IsNativeStartPage)
            return false;

        var existing = Bookmarks.FirstOrDefault(item => string.Equals(item.Url, tab.Url, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            Bookmarks.Remove(existing);
            return false;
        }

        Bookmarks.Insert(0, new BookmarkEntry { Title = tab.Title, Url = tab.Url });
        return true;
    }

    public bool IsBookmarked(string? url) =>
        !string.IsNullOrWhiteSpace(url) &&
        Bookmarks.Any(item => string.Equals(item.Url, url, StringComparison.OrdinalIgnoreCase));


    public void UpdateDownload(string filePath, int status, long bytesReceived, long bytesExpected)
    {
        var entry = Downloads.FirstOrDefault(item => string.Equals(item.FilePath, filePath, StringComparison.OrdinalIgnoreCase));
        if (entry is null)
        {
            entry = new DownloadEntry { FilePath = filePath };
            Downloads.Insert(0, entry);
        }

        entry.Status = status switch
        {
            0 => "Starting",
            1 => "Downloading",
            2 => "Completed",
            3 => "Failed",
            _ => "Unknown"
        };
        entry.BytesReceived = bytesReceived;
        entry.BytesExpected = bytesExpected;
        entry.UpdatedAt = DateTimeOffset.Now;
        _ = SaveAsync();
    }

    public bool IsPermissionAllowed(string origin, int permissionType)
    {
        if (!_permissionIndex.TryGetValue(origin, out var permission))
            return false;
        return permissionType switch
        {
            0 => permission.GeolocationAllowed,
            1 => permission.NotificationsAllowed,
            _ => false
        };
    }

    public void RecordPermissionRequest(string origin, int permissionType)
    {
        if (string.IsNullOrWhiteSpace(origin))
            return;
        if (!_permissionIndex.TryGetValue(origin, out var permission))
        {
            permission = new SitePermissionEntry { Origin = origin };
            _permissionIndex[origin] = permission;
            SitePermissions.Add(permission);
        }
        permission.LastRequestedAt = DateTimeOffset.Now;
        StatusMessage = permissionType switch
        {
            0 => $"Location access from {origin} was blocked until you allow it in Settings.",
            1 => $"Notifications from {origin} were blocked until you allow them in Settings.",
            _ => $"Camera and microphone access from {origin} is blocked."
        };
        _ = SaveAsync();
    }

    public void ApplySitePermission(SitePermissionEntry entry, bool locationAllowed, bool notificationsAllowed)
    {
        entry.GeolocationAllowed = locationAllowed;
        entry.NotificationsAllowed = notificationsAllowed;
        _permissionIndex[entry.Origin] = entry;
    }

    public async Task ClearHistoryAsync()
    {
        History.Clear();
        await SaveAsync();
    }

    public async Task SaveAsync()
    {
        await _saveGate.WaitAsync();
        try
        {
            Directory.CreateDirectory(_dataDirectory);
            var selectedIndex = SelectedTab is null ? 0 : Math.Max(0, Tabs.IndexOf(SelectedTab));
            var session = new SessionState
            {
                TabUrls = Tabs.Select(tab => tab.Url).ToList(),
                SelectedIndex = selectedIndex
            };

            await WriteAsync("settings.json", Settings);
            await WriteAsync("bookmarks.json", Bookmarks.ToList());
            await WriteAsync("history.json", History.ToList());
            await WriteAsync("downloads.json", Downloads.ToList());
            await WriteAsync("permissions.json", SitePermissions.ToList());
            await WriteAsync("session.json", session);
        }
        finally
        {
            _saveGate.Release();
        }
    }

    private async Task<T?> ReadAsync<T>(string fileName)
    {
        var path = Path.Combine(_dataDirectory, fileName);
        if (!File.Exists(path))
            return default;
        try
        {
            var json = await File.ReadAllTextAsync(path);
            return JsonSerializer.Deserialize<T>(json, JsonOptions);
        }
        catch (JsonException)
        {
            return default;
        }
        catch (IOException)
        {
            return default;
        }
    }

    private async Task WriteAsync<T>(string fileName, T value)
    {
        var path = Path.Combine(_dataDirectory, fileName);
        var temporaryPath = path + ".tmp";
        var json = JsonSerializer.Serialize(value, JsonOptions);
        await File.WriteAllTextAsync(temporaryPath, json);
        File.Move(temporaryPath, path, overwrite: true);
    }

    private static string NormalizeAddress(string input, BrowserSettings settings)
    {
        if (string.Equals(input, BrowserSettings.NativeStartPageUrl, StringComparison.OrdinalIgnoreCase))
            return BrowserSettings.NativeStartPageUrl;

        if (Uri.TryCreate(input, UriKind.Absolute, out var absolute) &&
            (absolute.Scheme == Uri.UriSchemeHttp || absolute.Scheme == Uri.UriSchemeHttps ||
             absolute.Scheme == "about" || absolute.Scheme == "file"))
            return absolute.ToString();

        if (input.Contains(' ') || !input.Contains('.'))
        {
            var query = Uri.EscapeDataString(input);
            var searchUrl = settings.SearchEngine switch
            {
                BrowserSearchEngine.Bing => "https://www.bing.com/search?q=",
                BrowserSearchEngine.DuckDuckGo => "https://duckduckgo.com/?q=",
                BrowserSearchEngine.Brave => "https://search.brave.com/search?q=",
                _ => "https://www.google.com/search?q="
            };
            return searchUrl + query;
        }

        return "https://" + input;
    }
}
