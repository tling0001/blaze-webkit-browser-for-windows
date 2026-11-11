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
    private readonly string _dataDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "WebKitBrowser");

    public ObservableCollection<BrowserTab> Tabs { get; } = [];
    public ObservableCollection<BookmarkEntry> Bookmarks { get; } = [];
    public ObservableCollection<HistoryEntry> History { get; } = [];

    public BrowserSettings Settings { get; private set; } = new();

    [ObservableProperty]
    public partial BrowserTab? SelectedTab { get; set; }

    [ObservableProperty]
    public partial string AddressText { get; set; } = string.Empty;

    [ObservableProperty]
    public partial string StatusMessage { get; set; } = string.Empty;

    partial void OnSelectedTabChanged(BrowserTab? value)
    {
        AddressText = value?.Url ?? string.Empty;
    }

    public async Task InitializeAsync()
    {
        Directory.CreateDirectory(_dataDirectory);
        Settings = await ReadAsync<BrowserSettings>("settings.json") ?? new BrowserSettings();

        var savedBookmarks = await ReadAsync<List<BookmarkEntry>>("bookmarks.json") ?? [];
        var savedHistory = await ReadAsync<List<HistoryEntry>>("history.json") ?? [];
        foreach (var bookmark in savedBookmarks)
            Bookmarks.Add(bookmark);
        foreach (var historyItem in savedHistory)
            History.Add(historyItem);

        var session = await ReadAsync<SessionState>("session.json");
        if (session?.TabUrls is { Count: > 0 })
        {
            foreach (var url in session.TabUrls)
                Tabs.Add(new BrowserTab { Url = url, Title = url });
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
        var tab = new BrowserTab { Url = initialUrl ?? "about:blank" };
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
        {
            if (Tabs.Count == 0)
                AddTab();
            else
                SelectedTab = Tabs[Math.Min(index, Tabs.Count - 1)];
        }
    }

    [RelayCommand]
    private void NavigateAddress(string? address)
    {
        if (SelectedTab is null || string.IsNullOrWhiteSpace(address))
            return;
        var url = NormalizeAddress(address.Trim());
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
        if (tab is null || string.IsNullOrWhiteSpace(tab.Url) || tab.Url == "about:blank")
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

    public async Task ClearHistoryAsync()
    {
        History.Clear();
        await SaveAsync();
    }

    public async Task SaveAsync()
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
        await WriteAsync("session.json", session);
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

    private Task WriteAsync<T>(string fileName, T value)
    {
        var path = Path.Combine(_dataDirectory, fileName);
        var json = JsonSerializer.Serialize(value, JsonOptions);
        return File.WriteAllTextAsync(path, json);
    }

    private static string NormalizeAddress(string input)
    {
        if (Uri.TryCreate(input, UriKind.Absolute, out var absolute) &&
            (absolute.Scheme == Uri.UriSchemeHttp || absolute.Scheme == Uri.UriSchemeHttps ||
             absolute.Scheme == "about" || absolute.Scheme == "file"))
            return absolute.ToString();

        if (input.Contains(' ') || !input.Contains('.'))
            return "https://www.google.com/search?q=" + Uri.EscapeDataString(input);

        return "https://" + input;
    }
}
