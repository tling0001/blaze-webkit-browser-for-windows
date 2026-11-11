using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using System.Numerics;
using Windows.Foundation;
using WebKitBrowser.Models;
using WebKitBrowser.Services;
using WebKitBrowser.ViewModels;

namespace WebKitBrowser;

public sealed partial class MainPage : Page
{
    public MainPageViewModel ViewModel { get; } = new();

    private readonly Dictionary<TabViewItem, BrowserTab> _tabItems = [];
    private DispatcherQueueTimer? _stateTimer;
    private bool _initialized;
    private bool _engineAvailable;

    public MainPage()
    {
        InitializeComponent();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_initialized)
            return;
        _initialized = true;

        await ViewModel.InitializeAsync();
        foreach (var tab in ViewModel.Tabs)
            AddTabItem(tab);

        if (ViewModel.SelectedTab is not null)
            TabStrip.SelectedItem = FindTabItem(ViewModel.SelectedTab);

        ApplyTabBarPlacement();
        CreateNativeViews();
        LayoutNativeViews();

        _stateTimer = App.DispatcherQueue.CreateTimer();
        _stateTimer.Interval = TimeSpan.FromMilliseconds(650);
        _stateTimer.Tick += OnStateTimerTick;
        _stateTimer.Start();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _stateTimer?.Stop();
        _stateTimer = null;
        _ = ViewModel.SaveAsync();
    }

    private void OnAddTabButtonClick(TabView sender, object args)
    {
        var tab = ViewModel.AddTab();
        var item = AddTabItem(tab);
        TabStrip.SelectedItem = item;
        EnsureNativeView(tab);
        LayoutNativeViews();
        _ = ViewModel.SaveAsync();
    }

    private void OnTabCloseRequested(TabView sender, TabViewTabCloseRequestedEventArgs args)
    {
        if (args.Tab is not TabViewItem item || !_tabItems.TryGetValue(item, out var tab))
            return;

        DestroyNativeView(tab);
        _tabItems.Remove(item);
        ViewModel.CloseTab(tab);
        TabStrip.TabItems.Remove(item);

        if (ViewModel.Tabs.Count == 0)
        {
            var replacement = ViewModel.AddTab();
            var replacementItem = AddTabItem(replacement);
            TabStrip.SelectedItem = replacementItem;
            EnsureNativeView(replacement);
        }
        else if (ViewModel.SelectedTab is not null)
            TabStrip.SelectedItem = FindTabItem(ViewModel.SelectedTab);

        UpdateClosableTabs();
        LayoutNativeViews();
        _ = ViewModel.SaveAsync();
    }

    private void OnTabSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (TabStrip.SelectedItem is not TabViewItem item || !_tabItems.TryGetValue(item, out var tab))
            return;

        ViewModel.SelectedTab = tab;
        AddressBox.Text = tab.Url;
        EnsureNativeView(tab);
        LayoutNativeViews();
        UpdateNavigationButtons();
        _ = ViewModel.SaveAsync();
    }

    private void OnBrowserSurfaceSizeChanged(object sender, SizeChangedEventArgs e) => LayoutNativeViews();

    private void OnBackClick(object sender, RoutedEventArgs e) => ViewModel.GoBackCommand.Execute(null);
    private void OnForwardClick(object sender, RoutedEventArgs e) => ViewModel.GoForwardCommand.Execute(null);
    private void OnReloadClick(object sender, RoutedEventArgs e) => ViewModel.ReloadCommand.Execute(null);

    private void OnHomeClick(object sender, RoutedEventArgs e)
    {
        ViewModel.NavigateAddressCommand.Execute(ViewModel.Settings.HomePage);
        AddressBox.Text = ViewModel.Settings.HomePage;
    }

    private async void OnAddressQuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
    {
        ViewModel.NavigateAddressCommand.Execute(args.QueryText);
        AddressBox.Text = ViewModel.AddressText;
        await ViewModel.SaveAsync();
    }

    private async void OnBookmarkClick(object sender, RoutedEventArgs e)
    {
        var added = ViewModel.ToggleBookmark(ViewModel.SelectedTab);
        await ViewModel.SaveAsync();
        if (sender is Button button)
            button.Content = added ? "Bookmarked" : "Bookmark";
    }

    private void OnHistoryClick(object sender, RoutedEventArgs e)
    {
        var flyout = new MenuFlyout();
        if (ViewModel.History.Count == 0)
            flyout.Items.Add(new MenuFlyoutItem { Text = "No browsing history yet", IsEnabled = false });
        else
        {
            foreach (var entry in ViewModel.History.Take(30))
            {
                var item = new MenuFlyoutItem { Text = string.IsNullOrWhiteSpace(entry.Title) ? entry.Url : entry.Title };
                item.Click += (_, _) => NavigateTo(entry.Url);
                flyout.Items.Add(item);
            }
            flyout.Items.Add(new MenuFlyoutSeparator());
            var clear = new MenuFlyoutItem { Text = "Clear history" };
            clear.Click += async (_, _) => await ViewModel.ClearHistoryAsync();
            flyout.Items.Add(clear);
        }
        if (sender is FrameworkElement element)
            flyout.ShowAt(element);
    }

    private void OnDownloadsClick(object sender, RoutedEventArgs e)
    {
        _ = ShowDownloadsStatusAsync();
    }

    private async Task ShowDownloadsStatusAsync()
    {
        var dialog = new ContentDialog
        {
            Title = "Downloads",
            Content = "The native WebKit download client is the next engine integration task. No downloads are currently tracked by this build.",
            CloseButtonText = "Close",
            XamlRoot = XamlRoot
        };
        await dialog.ShowAsync();
    }

    private async void OnSettingsClick(object sender, RoutedEventArgs e)
    {
        var position = new ComboBox
        {
            Header = "Tab bar position",
            ItemsSource = new[] { "Above toolbar", "Below toolbar" },
            SelectedIndex = ViewModel.Settings.TabBarAboveToolbar ? 0 : 1,
            MinWidth = 260
        };
        var javaScript = new ToggleSwitch
        {
            Header = "Enable JavaScript",
            IsOn = ViewModel.Settings.JavaScriptEnabled
        };
        var images = new ToggleSwitch
        {
            Header = "Load images automatically",
            IsOn = ViewModel.Settings.LoadImagesAutomatically
        };
        var clearData = new Button
        {
            Content = "Clear cookies, caches and site storage",
            HorizontalAlignment = HorizontalAlignment.Left
        };
        clearData.Click += (_, _) =>
        {
            foreach (var tab in ViewModel.Tabs)
                if (tab.NativeHandle != IntPtr.Zero)
                    WebKitNative.ClearWebsiteData(tab.NativeHandle);
            ViewModel.History.Clear();
            _ = ViewModel.SaveAsync();
        };

        var panel = new StackPanel { Spacing = 14, MinWidth = 320 };
        panel.Children.Add(position);
        panel.Children.Add(javaScript);
        panel.Children.Add(images);
        panel.Children.Add(new TextBlock
        {
            Text = "Privacy",
            Style = (Style)Application.Current.Resources["SubtitleTextBlockStyle"]
        });
        panel.Children.Add(clearData);

        var dialog = new ContentDialog
        {
            Title = "Settings",
            Content = panel,
            PrimaryButtonText = "Save",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = XamlRoot
        };

        if (await dialog.ShowAsync() != ContentDialogResult.Primary)
            return;

        ViewModel.Settings.TabBarAboveToolbar = position.SelectedIndex == 0;
        ViewModel.Settings.JavaScriptEnabled = javaScript.IsOn;
        ViewModel.Settings.LoadImagesAutomatically = images.IsOn;
        foreach (var tab in ViewModel.Tabs)
        {
            if (tab.NativeHandle == IntPtr.Zero)
                continue;
            WebKitNative.SetJavaScriptEnabled(tab.NativeHandle, javaScript.IsOn);
            WebKitNative.SetLoadsImagesAutomatically(tab.NativeHandle, images.IsOn);
        }
        ApplyTabBarPlacement();
        LayoutNativeViews();
        await ViewModel.SaveAsync();
    }

    private void OnStateTimerTick(DispatcherQueueTimer sender, object args)
    {
        var tab = ViewModel.SelectedTab;
        if (tab is null || tab.NativeHandle == IntPtr.Zero)
            return;

        var previousUrl = tab.Url;
        tab.RefreshNativeState();
        AddressBox.Text = tab.Url;
        if (!string.Equals(previousUrl, tab.Url, StringComparison.Ordinal) &&
            !string.Equals(tab.LastRecordedUrl, tab.Url, StringComparison.Ordinal))
        {
            ViewModel.RecordNavigation(tab);
            _ = ViewModel.SaveAsync();
        }

        if (FindTabItem(tab) is { } item)
            item.Header = tab.Title.Length > 28 ? tab.Title[..28] + "…" : tab.Title;
        UpdateNavigationButtons();
    }

    private void NavigateTo(string url)
    {
        ViewModel.NavigateAddressCommand.Execute(url);
        AddressBox.Text = ViewModel.AddressText;
        _ = ViewModel.SaveAsync();
    }

    private TabViewItem AddTabItem(BrowserTab tab)
    {
        var item = new TabViewItem
        {
            Header = string.IsNullOrWhiteSpace(tab.Title) ? "New tab" : tab.Title,
            IsClosable = true,
            Content = new Grid { Width = 0, Height = 0 }
        };
        _tabItems[item] = tab;
        TabStrip.TabItems.Add(item);
        UpdateClosableTabs();
        return item;
    }

    private TabViewItem? FindTabItem(BrowserTab tab) =>
        _tabItems.FirstOrDefault(pair => ReferenceEquals(pair.Value, tab)).Key;

    private void UpdateClosableTabs()
    {
        foreach (var item in _tabItems.Keys)
            item.IsClosable = ViewModel.Tabs.Count > 1;
    }

    private void ApplyTabBarPlacement()
    {
        var tabRow = ViewModel.Settings.TabBarAboveToolbar ? 0 : 1;
        var toolbarRow = ViewModel.Settings.TabBarAboveToolbar ? 1 : 0;
        Grid.SetRow(TabStrip, tabRow);
        Grid.SetRow(Toolbar, toolbarRow);
    }

    private void CreateNativeViews()
    {
        foreach (var tab in ViewModel.Tabs)
            EnsureNativeView(tab);
    }

    private void EnsureNativeView(BrowserTab tab)
    {
        if (tab.NativeHandle != IntPtr.Zero || !_initialized)
            return;

        try
        {
            var (x, y, width, height) = GetBrowserSurfaceBounds();
            tab.NativeHandle = WebKitNative.Create(App.WindowHandle, x, y, Math.Max(1, width), Math.Max(1, height), tab.Url);
            if (tab.NativeHandle == IntPtr.Zero)
                throw new InvalidOperationException("WKViewCreate did not return a native view.");
            WebKitNative.SetJavaScriptEnabled(tab.NativeHandle, ViewModel.Settings.JavaScriptEnabled);
            WebKitNative.SetLoadsImagesAutomatically(tab.NativeHandle, ViewModel.Settings.LoadImagesAutomatically);
            _engineAvailable = true;
            EngineError.IsOpen = false;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException or InvalidOperationException)
        {
            _engineAvailable = false;
            EngineError.Message = $"The pinned WinCairo runtime failed to initialize: {ex.Message}";
            EngineError.IsOpen = true;
        }
    }

    private void DestroyNativeView(BrowserTab tab)
    {
        if (tab.NativeHandle == IntPtr.Zero)
            return;
        WebKitNative.Destroy(tab.NativeHandle);
        tab.NativeHandle = IntPtr.Zero;
    }

    private void LayoutNativeViews()
    {
        if (!_initialized || !_engineAvailable)
            return;

        var (x, y, width, height) = GetBrowserSurfaceBounds();
        foreach (var tab in ViewModel.Tabs)
        {
            if (tab.NativeHandle == IntPtr.Zero)
                continue;
            if (ReferenceEquals(tab, ViewModel.SelectedTab))
                WebKitNative.SetBounds(tab.NativeHandle, x, y, width, height);
            else
                WebKitNative.SetBounds(tab.NativeHandle, 0, 0, 0, 0);
        }
    }

    private (int X, int Y, int Width, int Height) GetBrowserSurfaceBounds()
    {
        var point = BrowserSurface.TransformToVisual(null).TransformPoint(new Point(0, 0));
        var scale = BrowserSurface.XamlRoot?.RasterizationScale ?? 1.0;
        var x = (int)Math.Round(point.X * scale);
        var y = (int)Math.Round(point.Y * scale);
        var width = (int)Math.Round(BrowserSurface.ActualWidth * scale);
        var height = (int)Math.Round(BrowserSurface.ActualHeight * scale);
        return (x, y, Math.Max(1, width), Math.Max(1, height));
    }

    private void UpdateNavigationButtons()
    {
        // Native back/forward state is reflected by the selected tab model.
        if (ViewModel.SelectedTab is null)
            return;
    }
}
