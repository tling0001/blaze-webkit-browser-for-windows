using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.UI.ViewManagement;
using WebKitBrowser.Diagnostics;
using WebKitBrowser.Models;
using WebKitBrowser.Services;
using WebKitBrowser.ViewModels;
using WebKitBrowser.Security;

namespace WebKitBrowser;

public sealed partial class MainPage : Page
{
    public MainPageViewModel ViewModel { get; } = new();

    public IReadOnlyList<StartPageShortcut> StartPageShortcuts { get; } =
    [
        new("Google", "https://www.google.com", "\uE721", "ShortcutGoogle"),
        new("Bing", "https://www.bing.com", "\uE721", "ShortcutBing"),
        new("DuckDuckGo", "https://duckduckgo.com", "\uE721", "ShortcutDuckDuckGo"),
        new("Brave Search", "https://search.brave.com", "\uE721", "ShortcutBraveSearch"),
        new("Wikipedia", "https://www.wikipedia.org", "\uE82D", "ShortcutWikipedia"),
        new("YouTube", "https://www.youtube.com", "\uE768", "ShortcutYouTube"),
        new("GitHub", "https://github.com", "\uE943", "ShortcutGitHub"),
        new("Outlook", "https://outlook.live.com", "\uE715", "ShortcutOutlook")
    ];

    private static readonly WebKitNative.DownloadCallback DownloadCallbackHandler = OnNativeDownload;
    private static readonly WebKitNative.PermissionCallback PermissionCallbackHandler = OnNativePermission;
    private static readonly WebKitNative.JavaScriptDialogCallback JavaScriptDialogCallbackHandler = OnNativeJavaScriptDialog;
    private static readonly WebKitNative.NewPageCallback NewPageCallbackHandler = OnNativeNewPage;
    private static readonly WebKitNative.ClosePageCallback ClosePageCallbackHandler = OnNativePageClose;
    private const string SafariWindowsUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/27.2 Safari/605.1.15";

    private readonly Dictionary<TabViewItem, BrowserTab> _tabItems = [];
    private readonly Dictionary<string, TabHeaderVisuals> _tabHeaders = [];
    private readonly System.Collections.Concurrent.ConcurrentDictionary<IntPtr, string> _nativeTabIds = new();
    private readonly HashSet<string> _closingTabIds = [];
    private readonly Dictionary<string, string> _requestedTabFaviconUrls = [];
    private readonly FaviconCache _faviconCache = new();
    private readonly HashSet<IntPtr> _pendingPopups = [];
    private readonly HashSet<IntPtr> _retainedNativeViews = [];
    private readonly Dictionary<string, UIElement> _internalPageRoots = [];
    private readonly SemaphoreSlim _dialogGate = new(1, 1);
    private DispatcherQueueTimer? _stateTimer;
    private bool _initialized;
    private bool _isPageUnloading;
    private bool _engineAvailable;
    private bool _isAddressEditing;
    private bool _internalPageOpen;
    private bool _addressSubmissionQueued;
    private GCHandle _nativeCallbackHandle;

    private sealed record TabHeaderVisuals(TextBlock Title, Image Favicon, FontIcon Fallback);

    public MainPage()
    {
        CrashLogger.Log("MainPage constructor entered.");
        try
        {
            InitializeComponent();
            CrashLogger.Log("MainPage XAML initialized.");
            PopulateStartPageShortcuts();
            CrashLogger.Log($"Created {StartPageShortcuts.Count} native start-page shortcut buttons.");
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("MainPage construction failed.", exception);
            throw;
        }
    }

    private void PopulateStartPageShortcuts()
    {
        foreach (var shortcut in StartPageShortcuts)
        {
            var fallback = new FontIcon
            {
                Glyph = shortcut.Glyph,
                FontSize = 24,
                FontFamily = new Microsoft.UI.Xaml.Media.FontFamily("Segoe Fluent Icons"),
                Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["AccentTextFillColorPrimaryBrush"]
            };
            var favicon = new Image
            {
                Width = 30,
                Height = 30,
                Stretch = Microsoft.UI.Xaml.Media.Stretch.Uniform,
                Visibility = Visibility.Collapsed
            };
            ConfigureFaviconImage(favicon, fallback);
            var iconHost = new Grid { Width = 32, Height = 32 };
            iconHost.Children.Add(fallback);
            iconHost.Children.Add(favicon);

            var title = new TextBlock
            {
                Text = shortcut.Title,
                Style = (Style)Application.Current.Resources["BodyTextBlockStyle"]
            };

            var content = new StackPanel
            {
                Spacing = 12,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };
            content.Children.Add(iconHost);
            content.Children.Add(title);

            var button = new Button
            {
                Width = 176,
                Height = 112,
                Padding = new Thickness(16),
                Tag = shortcut.Url,
                Content = content
            };
            button.Click += OnStartPageShortcutClick;
            AutomationProperties.SetName(button, shortcut.Title);
            AutomationProperties.SetAutomationId(button, shortcut.AutomationId);
            StartPageShortcutsGrid.Items.Add(button);
            _ = LoadFaviconIntoAsync(shortcut.Url, favicon, fallback);
        }
    }

    private static Microsoft.UI.Xaml.Media.ImageSource CreateFaviconImageSource(string path)
    {
        var uri = new Uri(path, UriKind.Absolute);
        return Path.GetExtension(path).Equals(".svg", StringComparison.OrdinalIgnoreCase)
            ? new SvgImageSource { UriSource = uri }
            : new BitmapImage(uri);
    }

    private static void ConfigureFaviconImage(Image image, FontIcon fallback)
    {
        // The enclosing tab item / shortcut button already has an accessible
        // name; keep these decorative site marks out of the screen-reader tree.
        AutomationProperties.SetAccessibilityView(image, Microsoft.UI.Xaml.Automation.Peers.AccessibilityView.Raw);
        AutomationProperties.SetAccessibilityView(fallback, Microsoft.UI.Xaml.Automation.Peers.AccessibilityView.Raw);
        image.ImageFailed += (_, _) =>
        {
            image.Visibility = Visibility.Collapsed;
            fallback.Visibility = Visibility.Visible;
        };
    }

    private async Task LoadFaviconIntoAsync(string pageUrl, Image image, FontIcon fallback)
    {
        try
        {
            var path = await _faviconCache.GetFaviconPathAsync(pageUrl);
            if (string.IsNullOrWhiteSpace(path) || _isPageUnloading)
                return;
            image.Source = CreateFaviconImageSource(path);
            image.Visibility = Visibility.Visible;
            fallback.Visibility = Visibility.Collapsed;
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("A website favicon could not be displayed; keeping the native fallback icon.", exception);
        }
    }

    private async Task RefreshTabFaviconAsync(BrowserTab tab, string pageUrl)
    {
        var path = await _faviconCache.GetFaviconPathAsync(pageUrl);
        if (_isPageUnloading || !string.Equals(tab.Url, pageUrl, StringComparison.OrdinalIgnoreCase)
            || !_tabHeaders.TryGetValue(tab.Id, out var header))
            return;
        if (string.IsNullOrWhiteSpace(path))
            return;

        try
        {
            header.Favicon.Source = CreateFaviconImageSource(path);
            header.Favicon.Visibility = Visibility.Visible;
            header.Fallback.Visibility = Visibility.Collapsed;
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("A tab favicon could not be decoded; keeping the native fallback icon.", exception);
        }
    }

    private void RequestTabFaviconIfNeeded(BrowserTab tab)
    {
        if (!Uri.TryCreate(tab.Url, UriKind.Absolute, out var uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
            return;
        if (_requestedTabFaviconUrls.TryGetValue(tab.Id, out var requested)
            && string.Equals(requested, tab.Url, StringComparison.OrdinalIgnoreCase))
            return;

        _requestedTabFaviconUrls[tab.Id] = tab.Url;
        if (_tabHeaders.TryGetValue(tab.Id, out var header))
        {
            header.Favicon.Source = null;
            header.Favicon.Visibility = Visibility.Collapsed;
            header.Fallback.Visibility = Visibility.Visible;
        }
        _ = RefreshTabFaviconAsync(tab, tab.Url);
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_initialized)
            return;
        _isPageUnloading = false;
        _initialized = true;

        await ViewModel.InitializeAsync();
        foreach (var tab in ViewModel.Tabs.Where(tab => IsInternalBrowserUrl(tab.Url)))
        {
            tab.Url = BrowserSettings.NativeStartPageUrl;
            tab.Title = "Blaze";
        }
        ApplyMotionPreferences();
        if (!_nativeCallbackHandle.IsAllocated)
            // Native download, dialog and popup callbacks can outlive a WKView; retain this bridge for the app process lifetime.
            _nativeCallbackHandle = GCHandle.Alloc(this);
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
        _isPageUnloading = true;
        _internalPageRoots.Clear();
        _stateTimer?.Stop();
        _stateTimer = null;
        _ = ViewModel.SaveAsync();
        foreach (var popup in _pendingPopups.ToArray())
            DestroyNativeViewHandleImmediately(popup);
        _pendingPopups.Clear();
        foreach (var pendingHandle in _retainedNativeViews.ToArray())
            FinalizeNativeViewDisposal(pendingHandle);
        foreach (var tab in ViewModel.Tabs)
            DestroyNativeView(tab);
    }

    private void OnAddTabButtonClick(TabView sender, object args)
    {
        CrashLogger.Log("Tab action: Add tab.");
        var tab = ViewModel.AddTab();
        var item = AddTabItem(tab);
        TabStrip.SelectedItem = item;
        EnsureNativeView(tab);
        LayoutNativeViews();
        _ = ViewModel.SaveAsync();
    }

    private void OnTabCloseRequested(TabView sender, TabViewTabCloseRequestedEventArgs args)
    {
        CrashLogger.Log("Tab action: Close requested.");
        if (args.Tab is TabViewItem item && _tabItems.TryGetValue(item, out var tab))
            CloseBrowserTab(tab);
    }

    private void CloseNativeTab(IntPtr nativeHandle, string tabId)
    {
        if (nativeHandle == IntPtr.Zero || string.IsNullOrEmpty(tabId))
            return;
        if (_pendingPopups.Remove(nativeHandle))
        {
            RetireNativeView(nativeHandle);
            return;
        }
        var tab = ViewModel.Tabs.FirstOrDefault(candidate =>
            candidate.Id == tabId && candidate.NativeHandle == nativeHandle);
        if (tab is not null)
            CloseBrowserTab(tab);
    }

    private void CloseBrowserTab(BrowserTab tab)
    {
        CrashLogger.Log("Tab action: Close tab.");
        if (!ViewModel.Tabs.Contains(tab) || !_closingTabIds.Add(tab.Id))
            return;

        try
        {
            var item = FindTabItem(tab);
            _internalPageRoots.Remove(tab.Id);
            var nativeHandle = tab.NativeHandle;
            // Clear identity before WB_Destroy: native teardown can trigger a
            // delayed WebKit close callback, which must never close a reused handle.
            tab.NativeHandle = IntPtr.Zero;
            if (nativeHandle != IntPtr.Zero)
            {
                CrashLogger.Log($"Tab close: retiring WebKit handle 0x{nativeHandle:X}.");
                _nativeTabIds.TryRemove(nativeHandle, out _);
                RetireNativeView(nativeHandle);
                CrashLogger.Log($"Tab close: native handle 0x{nativeHandle:X} detached and hidden; native WKView remains retained.");
            }

            if (item is not null)
            {
                _tabItems.Remove(item);
                _tabHeaders.Remove(tab.Id);
                _requestedTabFaviconUrls.Remove(tab.Id);
                TabStrip.TabItems.Remove(item);
            }
            CrashLogger.Log("Tab close: removing tab from the WinUI TabView and view model.");
            ViewModel.CloseTab(tab);
            CrashLogger.Log("Tab close: view-model removal completed.");

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
            CrashLogger.Log("Tab close: laying out remaining native WebKit views.");
            LayoutNativeViews();
            CrashLogger.Log("Tab close: remaining view layout completed.");
            _ = ViewModel.SaveAsync();
        }
        finally
        {
            _closingTabIds.Remove(tab.Id);
        }
    }

    private void AttachPopupTab(IntPtr nativeHandle)
    {
        if (nativeHandle == IntPtr.Zero)
            return;
        if (!_initialized || _isPageUnloading)
        {
            WebKitNative.Destroy(nativeHandle);
            return;
        }

        // A popup owns a real WebKit page immediately; it must not inherit the
        // native start-page URL used by ordinary new tabs.
        var tab = ViewModel.AddTab("about:blank");
        tab.NativeHandle = nativeHandle;
        _nativeTabIds[nativeHandle] = tab.Id;
        var item = AddTabItem(tab);
        ConfigureNativeView(tab);
        TabStrip.SelectedItem = item;
        UpdateClosableTabs();
        LayoutNativeViews();
        _ = ViewModel.SaveAsync();
    }

    private void QueuePopupPermission(IntPtr nativeHandle)
    {
        if (nativeHandle == IntPtr.Zero)
            return;
        if (!_initialized || _isPageUnloading || !_pendingPopups.Add(nativeHandle))
        {
            WebKitNative.Destroy(nativeHandle);
            return;
        }
        _ = RequestPopupPermissionAsync(nativeHandle);
    }

    private async Task RequestPopupPermissionAsync(IntPtr nativeHandle)
    {
        try
        {
            if (!_initialized || _isPageUnloading)
                return;

            var source = "A website";
            if (Uri.TryCreate(ViewModel.SelectedTab?.Url, UriKind.Absolute, out var uri)
                && (uri.Scheme == Uri.UriSchemeHttp || uri.Scheme == Uri.UriSchemeHttps))
                source = uri.Host;

            var dialog = new ContentDialog
            {
                Title = "Allow pop-up?",
                Content = $"{source} wants to open a new browser tab. Only allow pop-ups from sites you trust.",
                PrimaryButtonText = "Allow",
                CloseButtonText = "Ignore",
                DefaultButton = ContentDialogButton.Close,
                XamlRoot = XamlRoot
            };
            AutomationProperties.SetAutomationId(dialog, "PopupPermissionDialog");
            AutomationProperties.SetName(dialog, "Pop-up permission request");

            var result = await ShowDialogAsync(dialog);
            if (result == ContentDialogResult.Primary && !_isPageUnloading && _pendingPopups.Remove(nativeHandle))
            {
                AttachPopupTab(nativeHandle);
                return;
            }
        }
        catch (Exception ex)
        {
            if (!_isPageUnloading)
                ViewModel.StatusMessage = $"The pop-up request could not be displayed: {ex.Message}";
        }
        finally
        {
            if (_pendingPopups.Remove(nativeHandle))
                RetireNativeView(nativeHandle);
        }
    }

    private void OnTabSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        CrashLogger.Log("Tab action: Selection changed.");
        if (TabStrip.SelectedItem is not TabViewItem item || !_tabItems.TryGetValue(item, out var tab))
            return;

        ViewModel.SelectedTab = tab;
        ShowInternalPageForSelectedTab();
        _isAddressEditing = false;
        SynchronizeAddressBoxFromSelectedTab();
        UpdateBookmarkButton();
        EnsureNativeView(tab);
        LayoutNativeViews();
        UpdateNavigationButtons();
        _ = ViewModel.SaveAsync();
    }

    private void OnBrowserSurfaceSizeChanged(object sender, SizeChangedEventArgs e) => LayoutNativeViews();

    private void OnFocusAddressAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        SynchronizeAddressBoxFromSelectedTab();
        AddressBox.Focus(FocusState.Programmatic);
        args.Handled = true;
    }

    private void OnNewTabAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        OnAddTabButtonClick(TabStrip, EventArgs.Empty);
        args.Handled = true;
    }

    private void OnCloseTabAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (ViewModel.SelectedTab is { } tab)
            CloseBrowserTab(tab);
        args.Handled = true;
    }

    private void OnReloadAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        ViewModel.ReloadCommand.Execute(null);
        args.Handled = true;
    }

    private void OnBackClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Back.");
        ViewModel.GoBackCommand.Execute(null);
    }

    private void OnForwardClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Forward.");
        ViewModel.GoForwardCommand.Execute(null);
    }
    private void OnReloadStopClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Reload/Stop.");
        if (ViewModel.SelectedTab?.IsLoading == true) ViewModel.StopCommand.Execute(null);
        else ViewModel.ReloadCommand.Execute(null);
        UpdateLoadingButton();
    }

    private void OnHomeClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Home.");
        CloseInternalPage();
        NavigateTo(ViewModel.Settings.HomePage);
    }

    private void OnAddressQuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args)
    {
        var query = args.QueryText?.Trim();
        var destination = args.ChosenSuggestion is AddressSuggestion chosen ? chosen.TargetUrl : query;
        CrashLogger.Log($"Address action: Submit navigation (suggestion: {args.ChosenSuggestion is AddressSuggestion}; query length: {query?.Length ?? 0}).");
        sender.IsSuggestionListOpen = false;
        _isAddressEditing = false;

        if (string.IsNullOrWhiteSpace(destination) || _addressSubmissionQueued)
            return;

        // AutoSuggestBox raises QuerySubmitted while it is still processing Enter.
        // Defer native WKPageLoadURL and focus changes until that routed input
        // event has completely unwound; this avoids re-entering WinUI/WebKit
        // from inside the address control's keyboard handling.
        _addressSubmissionQueued = true;
        if (!App.DispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                if (_isPageUnloading)
                {
                    _addressSubmissionQueued = false;
                    return;
                }

                NavigateTo(destination!);
                // Give AutoSuggestBox one more dispatcher turn to finish its
                // internal Enter/suggestion-popup focus work. Focusing in the
                // QuerySubmitted turn can be immediately undone by that control.
                if (!App.DispatcherQueue.TryEnqueue(() =>
                {
                    try
                    {
                        if (_isPageUnloading)
                            return;

                        _isAddressEditing = false;
                        var focused = BackButton.Focus(FocusState.Programmatic);
                        if (!focused)
                            focused = TabStrip.Focus(FocusState.Programmatic);
                        if (AddressBox.FocusState != FocusState.Unfocused)
                            focused = TabStrip.Focus(FocusState.Programmatic) || focused;

                        CrashLogger.Log($"Address submission focus result: moved={focused}; addressFocus={AddressBox.FocusState}.");
                        if (AddressBox.FocusState != FocusState.Unfocused)
                            ViewModel.StatusMessage = "The page loaded, but keyboard focus remained in the address bar.";
                    }
                    catch (Exception exception)
                    {
                        CrashLogger.LogException("Address-bar focus restoration failed.", exception);
                    }
                    finally
                    {
                        _addressSubmissionQueued = false;
                    }
                }))
                {
                    _addressSubmissionQueued = false;
                    CrashLogger.Log("Address-bar focus restoration could not be queued.");
                }
            }
            catch (Exception exception)
            {
                _addressSubmissionQueued = false;
                CrashLogger.LogException("Address-bar navigation failed.", exception);
                ViewModel.StatusMessage = "The address could not be opened. Check the crash log for details.";
            }
        }))
        {
            _addressSubmissionQueued = false;
            CrashLogger.Log("Address navigation could not be queued because the UI dispatcher is shutting down.");
        }
    }

    private void OnAddressGotFocus(object sender, RoutedEventArgs e) => _isAddressEditing = true;

    private void OnAddressLostFocus(object sender, RoutedEventArgs e)
    {
        // Let AutoSuggestBox finish moving focus through its internal text box / suggestion surface.
        App.DispatcherQueue.TryEnqueue(() =>
        {
            if (AddressBox.FocusState == FocusState.Unfocused)
            {
                _isAddressEditing = false;
                SynchronizeAddressBoxFromSelectedTab();
            }
        });
    }

    private void OnAddressTextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
    {
        if (args.Reason != AutoSuggestionBoxTextChangeReason.UserInput)
            return;

        _isAddressEditing = true;
        var suggestions = ViewModel.GetAddressSuggestions(sender.Text);
        sender.ItemsSource = suggestions;
        sender.IsSuggestionListOpen = suggestions.Count > 0;
    }

    private void OnAddressSuggestionChosen(AutoSuggestBox sender, AutoSuggestBoxSuggestionChosenEventArgs args)
    {
        if (args.SelectedItem is AddressSuggestion suggestion)
            sender.Text = suggestion.Title;
    }

    private void SynchronizeAddressBoxFromSelectedTab()
    {
        var tab = ViewModel.SelectedTab;
        AddressBox.Text = tab is null || tab.IsNativeStartPage ? string.Empty : tab.Url;
    }

    private void OnStartPageSearchQuerySubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args) =>
        NavigateTo(args.QueryText);

    private void OnStartPageShortcutClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string url })
            NavigateTo(url);
    }

    private async void OnBookmarkClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Toggle bookmark.");
        ViewModel.ToggleBookmark(ViewModel.SelectedTab);
        await ViewModel.SaveAsync();
        UpdateBookmarkButton();
    }

    private void OnBookmarksClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Open bookmarks.");
        var flyout = CreateMenuFlyout();
        if (ViewModel.Bookmarks.Count == 0)
            flyout.Items.Add(new MenuFlyoutItem { Text = "No saved bookmarks yet", IsEnabled = false });
        else
        {
            foreach (var bookmark in ViewModel.Bookmarks)
            {
                var item = new MenuFlyoutItem { Text = string.IsNullOrWhiteSpace(bookmark.Title) ? bookmark.Url : bookmark.Title };
                item.Click += (_, _) => NavigateTo(bookmark.Url);
                flyout.Items.Add(item);
            }
        }
        if (sender is FrameworkElement element)
            flyout.ShowAt(element);
    }

    private void OnHistoryClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Open history page.");
        ShowHistoryPage();
    }

    private void ShowHistoryPage()
    {
        var root = new Grid { Padding = new Thickness(28, 24, 28, 20), RowSpacing = 16, Transitions = CreateEntranceTransitions() };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var heading = new Grid { ColumnSpacing = 14 };
        heading.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        heading.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var titleStack = new StackPanel { Spacing = 4 };
        titleStack.Children.Add(new TextBlock { Text = "History", Style = (Style)Application.Current.Resources["TitleTextBlockStyle"] });
        titleStack.Children.Add(new TextBlock { Text = "Pages you have visited in Blaze", Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["TextFillColorSecondaryBrush"] });
        heading.Children.Add(titleStack);
        var close = new Button { Content = new FontIcon { Glyph = "\uE72B", FontFamily = new Microsoft.UI.Xaml.Media.FontFamily("Segoe Fluent Icons") }, Width = 40, Height = 40 };
        AutomationProperties.SetName(close, "Close history");
        AutomationProperties.SetAutomationId(close, "HistoryBackButton");
        close.Click += (_, _) => CloseInternalPage();
        Grid.SetColumn(close, 1); heading.Children.Add(close); root.Children.Add(heading);

        var search = new TextBox { PlaceholderText = "Search history" };
        AutomationProperties.SetName(search, "Search browsing history");
        AutomationProperties.SetAutomationId(search, "HistorySearchBox");
        Grid.SetRow(search, 1); root.Children.Add(search);
        var list = new ListView { SelectionMode = ListViewSelectionMode.None };
        AutomationProperties.SetName(list, "Browsing history entries");
        AutomationProperties.SetAutomationId(list, "HistoryEntries");
        Grid.SetRow(list, 2); root.Children.Add(list);

        void RefreshEntries()
        {
            list.Items.Clear();
            var query = search.Text?.Trim() ?? string.Empty;
            foreach (var entry in ViewModel.History)
            {
                var title = string.IsNullOrWhiteSpace(entry.Title) ? entry.Url : entry.Title;
                if (query.Length > 0 && !title.Contains(query, StringComparison.OrdinalIgnoreCase)
                    && !entry.Url.Contains(query, StringComparison.OrdinalIgnoreCase)) continue;
                var content = new StackPanel { Spacing = 3 };
                content.Children.Add(new TextBlock { Text = title, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis });
                content.Children.Add(new TextBlock { Text = entry.Url, Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["TextFillColorSecondaryBrush"], TextTrimming = TextTrimming.CharacterEllipsis });
                content.Children.Add(new TextBlock { Text = entry.VisitedAt.ToLocalTime().ToString("g"), Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["TextFillColorTertiaryBrush"], FontSize = 12 });
                var open = new Button { HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Padding = new Thickness(12, 9, 12, 9), Content = content };
                AutomationProperties.SetName(open, "Open " + title);
                open.Click += (_, _) => { CloseInternalPage(); NavigateTo(entry.Url); };
                list.Items.Add(open);
            }
            if (list.Items.Count == 0)
                list.Items.Add(new TextBlock { Text = query.Length == 0 ? "No browsing history yet." : "No history matches your search.", Margin = new Thickness(12, 18, 12, 18), Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["TextFillColorSecondaryBrush"] });
        }
        search.TextChanged += (_, _) => RefreshEntries();
        RefreshEntries();

        var footer = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Spacing = 8 };
        var clear = new Button { Content = "Clear history" };
        AutomationProperties.SetAutomationId(clear, "ClearHistoryButton");
        clear.Click += async (_, _) =>
        {
            var confirmation = new ContentDialog { Title = "Clear browsing history?", Content = "This removes the browsing history saved by Blaze. Cookies and website data are not affected.", PrimaryButtonText = "Clear history", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close, XamlRoot = XamlRoot };
            if (await ShowDialogAsync(confirmation) == ContentDialogResult.Primary) { await ViewModel.ClearHistoryAsync(); RefreshEntries(); }
        };
        var done = new Button { Content = "Done" };
        done.Click += (_, _) => CloseInternalPage();
        footer.Children.Add(clear); footer.Children.Add(done);
        Grid.SetRow(footer, 3); root.Children.Add(footer);
        OpenInternalPage("blaze://history", "History", root);
    }

    private void OpenInternalPage(string url, string title, UIElement root)
    {
        var tab = ViewModel.SelectedTab;
        if (tab is null) return;
        if (!tab.IsNativeStartPage)
        {
            tab = ViewModel.AddTab(BrowserSettings.NativeStartPageUrl);
            var item = AddTabItem(tab);
            TabStrip.SelectedItem = item;
        }
        tab.Url = url;
        tab.Title = title;
        tab.CanGoBack = false;
        tab.CanGoForward = false;
        _internalPageRoots[tab.Id] = root;
        if (_tabHeaders.TryGetValue(tab.Id, out var header)) header.Title.Text = title;
        if (FindTabItem(tab) is { } tabItem) AutomationProperties.SetName(tabItem, title);
        ViewModel.AddressText = url;
        AddressBox.Text = url;
        ShowInternalPageForSelectedTab();
        LayoutNativeViews();
        UpdateNavigationButtons();
        _ = ViewModel.SaveAsync();
    }

    private void ShowInternalPageForSelectedTab()
    {
        InternalPageSurface.Children.Clear();
        _internalPageOpen = false;
        var tab = ViewModel.SelectedTab;
        if (tab is not null && _internalPageRoots.TryGetValue(tab.Id, out var root))
        {
            InternalPageSurface.Children.Add(root);
            _internalPageOpen = true;
        }
        InternalPageSurface.Visibility = _internalPageOpen ? Visibility.Visible : Visibility.Collapsed;
    }

    private void DiscardSelectedInternalPage()
    {
        if (ViewModel.SelectedTab is not { } tab || !_internalPageRoots.Remove(tab.Id)) return;
        InternalPageSurface.Children.Clear();
        _internalPageOpen = false;
        InternalPageSurface.Visibility = Visibility.Collapsed;
    }

    private void CloseInternalPage()
    {
        if (ViewModel.SelectedTab is not { } tab || !_internalPageRoots.Remove(tab.Id)) return;
        InternalPageSurface.Children.Clear();
        _internalPageOpen = false;
        tab.Url = BrowserSettings.NativeStartPageUrl;
        tab.Title = "Blaze";
        tab.CanGoBack = false;
        tab.CanGoForward = false;
        ViewModel.AddressText = string.Empty;
        AddressBox.Text = string.Empty;
        if (_tabHeaders.TryGetValue(tab.Id, out var header)) header.Title.Text = "Blaze";
        if (FindTabItem(tab) is { } item) AutomationProperties.SetName(item, "Blaze");
        InternalPageSurface.Visibility = Visibility.Collapsed;
        if (_initialized && !_isPageUnloading) LayoutNativeViews();
        _ = ViewModel.SaveAsync();
    }

    private void OnDownloadsClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Open downloads.");
        var flyout = CreateMenuFlyout();
        if (ViewModel.Downloads.Count == 0)
            flyout.Items.Add(new MenuFlyoutItem { Text = "No downloads yet", IsEnabled = false });
        else
        {
            foreach (var download in ViewModel.Downloads.Take(20))
            {
                var item = new MenuFlyoutItem
                {
                    Text = $"{download.FileName} — {download.ProgressText}",
                    IsEnabled = download.Status == "Completed" && File.Exists(download.FilePath)
                };
                item.Click += (_, _) => Process.Start(new ProcessStartInfo(download.FilePath) { UseShellExecute = true });
                flyout.Items.Add(item);
            }
        }

        flyout.Items.Add(new MenuFlyoutSeparator());
        var openFolder = new MenuFlyoutItem { Text = "Open Downloads folder" };
        openFolder.Click += (_, _) => Process.Start(new ProcessStartInfo(
            Environment.GetFolderPath(Environment.SpecialFolder.UserProfile) + "\\Downloads") { UseShellExecute = true });
        flyout.Items.Add(openFolder);
        if (sender is FrameworkElement element)
            flyout.ShowAt(element);
    }

    private bool AreAnimationsEnabled => !ViewModel.Settings.ReduceMotion && new UISettings().AnimationsEnabled;

    private MenuFlyout CreateMenuFlyout() => new()
    {
        AreOpenCloseAnimationsEnabled = AreAnimationsEnabled
    };

    private void OnSettingsClick(object sender, RoutedEventArgs e)
    {
        CrashLogger.Log("Toolbar action: Open settings page.");
        ShowSettingsPage();
    }

    private TransitionCollection CreateEntranceTransitions()
    {
        if (!AreAnimationsEnabled)
            return [];

        return [new EntranceThemeTransition { IsStaggeringEnabled = false }];
    }

    private void ApplyMotionPreferences()
    {
        LayoutRoot.Transitions = CreateEntranceTransitions();
        TabStrip.Transitions = CreateEntranceTransitions();
        Toolbar.Transitions = CreateEntranceTransitions();
        PageLoadingProgress.Transitions = CreateEntranceTransitions();
        BrowserSurface.Transitions = CreateEntranceTransitions();
        StartPageSurface.Transitions = CreateEntranceTransitions();
        InternalPageSurface.Transitions = CreateEntranceTransitions();
        StartPageShortcutsGrid.Transitions = CreateEntranceTransitions();
    }

    private async Task<ContentDialogResult> ShowDialogAsync(ContentDialog dialog)
    {
        // The WebKit host is an owned top-level HWND and intentionally sits above
        // the WinUI composition surface. Hide it while any modal WinUI surface is
        // active so dialogs, pickers and their dimming layer remain visible.
        await _dialogGate.WaitAsync();
        try
        {
            SuspendNativeViewsForDialog();
            dialog.Transitions = CreateEntranceTransitions();
            dialog.ContentTransitions = CreateEntranceTransitions();
            return await dialog.ShowAsync();
        }
        finally
        {
            if (!_isPageUnloading)
                LayoutNativeViews();
            _dialogGate.Release();
        }
    }

    private void SuspendNativeViewsForDialog()
    {
        foreach (var tab in ViewModel.Tabs)
        {
            if (tab.NativeHandle != IntPtr.Zero)
                WebKitNative.SetBounds(tab.NativeHandle, 0, 0, 0, 0);
        }
    }

    private void ShowSettingsPage()
    {
        var position = new RadioButtons
        {
            Header = "Tab bar position",
            ItemsSource = new[] { "Above toolbar", "Below toolbar" },
            SelectedIndex = ViewModel.Settings.TabBarAboveToolbar ? 0 : 1
        };
        var homePage = new TextBox
        {
            Header = "Home page",
            Text = ViewModel.Settings.HomePageCustomized ? ViewModel.Settings.HomePage : string.Empty,
            PlaceholderText = "Leave blank to use the native Blaze start page"
        };
        var searchEngine = new RadioButtons { Header = "Search engine" };
        var googleSearchOption = new RadioButton { Content = "Google" };
        var bingSearchOption = new RadioButton { Content = "Bing" };
        var duckDuckGoSearchOption = new RadioButton { Content = "DuckDuckGo" };
        var braveSearchOption = new RadioButton { Content = "Brave Search" };
        AutomationProperties.SetAutomationId(googleSearchOption, "SearchEngineGoogle");
        AutomationProperties.SetAutomationId(bingSearchOption, "SearchEngineBing");
        AutomationProperties.SetAutomationId(duckDuckGoSearchOption, "SearchEngineDuckDuckGo");
        AutomationProperties.SetAutomationId(braveSearchOption, "SearchEngineBrave");
        AutomationProperties.SetName(googleSearchOption, "Google");
        AutomationProperties.SetName(bingSearchOption, "Bing");
        AutomationProperties.SetName(duckDuckGoSearchOption, "DuckDuckGo");
        AutomationProperties.SetName(braveSearchOption, "Brave Search");
        searchEngine.Items.Add(googleSearchOption);
        searchEngine.Items.Add(bingSearchOption);
        searchEngine.Items.Add(duckDuckGoSearchOption);
        searchEngine.Items.Add(braveSearchOption);
        searchEngine.SelectedIndex = Math.Clamp((int)ViewModel.Settings.SearchEngine, 0, 3);
        AutomationProperties.SetAutomationId(homePage, "HomePage");
        AutomationProperties.SetName(homePage, "Home page");
        AutomationProperties.SetAutomationId(searchEngine, "SearchEngine");
        AutomationProperties.SetName(searchEngine, "Search engine");

        var javaScript = new ToggleSwitch { Header = "Enable JavaScript", IsOn = ViewModel.Settings.JavaScriptEnabled };
        var images = new ToggleSwitch { Header = "Load images automatically", IsOn = ViewModel.Settings.LoadImagesAutomatically };
        var reduceMotion = new ToggleSwitch { Header = "Reduce motion", IsOn = ViewModel.Settings.ReduceMotion };
        reduceMotion.OffContent = "Animations on";
        reduceMotion.OnContent = "Reduced animations";
        var popupHandling = new RadioButtons { Header = "Pop-up windows" };
        var automaticPopupOption = new RadioButton { Content = "Automatic" };
        var allowPopupOption = new RadioButton { Content = "Always allow" };
        var denyPopupOption = new RadioButton { Content = "Always deny" };
        AutomationProperties.SetAutomationId(automaticPopupOption, "PopupModeAutomatic");
        AutomationProperties.SetAutomationId(allowPopupOption, "PopupModeAlwaysAllow");
        AutomationProperties.SetAutomationId(denyPopupOption, "PopupModeAlwaysDeny");
        AutomationProperties.SetName(automaticPopupOption, "Automatic");
        AutomationProperties.SetName(allowPopupOption, "Always allow");
        AutomationProperties.SetName(denyPopupOption, "Always deny");
        popupHandling.Items.Add(automaticPopupOption);
        popupHandling.Items.Add(allowPopupOption);
        popupHandling.Items.Add(denyPopupOption);
        popupHandling.SelectedIndex = (int)ViewModel.Settings.PopupHandlingMode;
        AutomationProperties.SetAutomationId(position, "TabBarPosition");
        AutomationProperties.SetAutomationId(javaScript, "JavaScriptEnabled");
        AutomationProperties.SetAutomationId(images, "LoadImagesAutomatically");
        AutomationProperties.SetAutomationId(reduceMotion, "ReduceMotion");
        AutomationProperties.SetAutomationId(popupHandling, "PopupHandlingMode");
        AutomationProperties.SetName(popupHandling, "Pop-up windows");

        var startupBehavior = new RadioButtons { Header = "When Blaze starts" };
        var startupNewTab = new RadioButton { Content = "Open a new tab" };
        var startupContinue = new RadioButton { Content = "Continue where you left off" };
        AutomationProperties.SetAutomationId(startupNewTab, "StartupNewTab");
        AutomationProperties.SetAutomationId(startupContinue, "StartupContinueSession");
        AutomationProperties.SetName(startupNewTab, "Open a new tab on launch");
        AutomationProperties.SetName(startupContinue, "Continue where you left off on launch");
        startupBehavior.Items.Add(startupNewTab);
        startupBehavior.Items.Add(startupContinue);
        startupBehavior.SelectedIndex = ViewModel.Settings.StartupBehavior == StartupBehavior.NewTab ? 0 : 1;
        AutomationProperties.SetAutomationId(startupBehavior, "StartupBehavior");
        AutomationProperties.SetName(startupBehavior, "Startup behavior");

        var generalPanel = new StackPanel { Spacing = 14, MinWidth = 420, Padding = new Thickness(12) };
        generalPanel.Children.Add(startupBehavior);
        generalPanel.Children.Add(position);
        generalPanel.Children.Add(homePage);
        generalPanel.Children.Add(searchEngine);

        var privacyPanel = new StackPanel { Spacing = 12, MinWidth = 420, Padding = new Thickness(12) };
        privacyPanel.Children.Add(javaScript);
        privacyPanel.Children.Add(images);
        privacyPanel.Children.Add(popupHandling);
        privacyPanel.Children.Add(new TextBlock
        {
            Text = "Automatic asks whether to allow each requested new window. Dismissing the prompt ignores that request.",
            TextWrapping = TextWrapping.Wrap
        });
        privacyPanel.Children.Add(new TextBlock
        {
            Text = "Site permissions",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold
        });

        var permissionControls = new List<(SitePermissionEntry Site, ToggleSwitch Location, ToggleSwitch Notifications)>();
        if (ViewModel.SitePermissions.Count == 0)
            privacyPanel.Children.Add(new TextBlock { Text = "No sites have requested location or notification access." });
        else
        {
            foreach (var site in ViewModel.SitePermissions)
            {
                privacyPanel.Children.Add(new TextBlock
                {
                    Text = site.Origin,
                    FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                    TextWrapping = TextWrapping.Wrap
                });
                var location = new ToggleSwitch { Header = "Allow location", IsOn = site.GeolocationAllowed };
                var notifications = new ToggleSwitch { Header = "Allow notifications", IsOn = site.NotificationsAllowed };
                AutomationProperties.SetAutomationId(location, "AllowLocation_" + SiteAutomationToken(site.Origin));
                AutomationProperties.SetAutomationId(notifications, "AllowNotifications_" + SiteAutomationToken(site.Origin));
                permissionControls.Add((site, location, notifications));
                privacyPanel.Children.Add(location);
                privacyPanel.Children.Add(notifications);
            }
        }
        privacyPanel.Children.Add(new TextBlock
        {
            Text = "Camera and microphone requests are denied until a device-selection permission flow is available.",
            TextWrapping = TextWrapping.Wrap
        });

        var appearancePanel = new StackPanel { Spacing = 12, MinWidth = 420, Padding = new Thickness(12) };
        appearancePanel.Children.Add(reduceMotion);
        appearancePanel.Children.Add(new TextBlock
        {
            Text = "Reduce motion respects both this preference and the Windows animation setting. When enabled, page and popup transitions are suppressed.",
            TextWrapping = TextWrapping.Wrap
        });

        var advancedPanel = new StackPanel { Spacing = 12, MinWidth = 420, Padding = new Thickness(12) };
        advancedPanel.Children.Add(new TextBlock
        {
            Text = "WebKit rendering host",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold
        });
        advancedPanel.Children.Add(new TextBlock
        {
            Text = "Win32 owned-window hosting is the active renderer. The WinCairo ANGLE shared-texture experiment is still diagnostic-only: the EGL import/render probe must pass, and WebKit WCScene frames must be presented through a WinUI composition surface before this can become a usable renderer.",
            TextWrapping = TextWrapping.Wrap
        });
        advancedPanel.Children.Add(new TextBlock
        {
            Text = "There is currently no experimental-renderer switch. It is intentionally unavailable while the ANGLE probe and end-to-end WebKit composition are incomplete. Blaze always uses the stable Win32 host in this build.",
            TextWrapping = TextWrapping.Wrap
        });

        var settingsTabs = new TabView
        {
            IsAddTabButtonVisible = false,
            CanReorderTabs = false,
            MinWidth = 480,
            MaxWidth = 720,
            MaxHeight = 620
        };
        AddSettingsTab(settingsTabs, "General", "SettingsGeneralTab", generalPanel);
        AddSettingsTab(settingsTabs, "Privacy", "SettingsPrivacyTab", privacyPanel);
        AddSettingsTab(settingsTabs, "Appearance", "SettingsAppearanceTab", appearancePanel);
        AddSettingsTab(settingsTabs, "Advanced", "SettingsAdvancedTab", advancedPanel);
        var passwordsPanel = CreatePasswordsPanel();
        AddSettingsTab(settingsTabs, "Passwords", "SettingsPasswordsTab", passwordsPanel);

        settingsTabs.MinWidth = 0;
        settingsTabs.MaxWidth = 960;
        settingsTabs.MaxHeight = double.PositiveInfinity;
        settingsTabs.HorizontalAlignment = HorizontalAlignment.Stretch;
        settingsTabs.VerticalAlignment = VerticalAlignment.Stretch;
        var root = new Grid { Padding = new Thickness(28, 24, 28, 20), RowSpacing = 16 };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var heading = new Grid { ColumnSpacing = 14 };
        heading.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        heading.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var titleStack = new StackPanel { Spacing = 4 };
        titleStack.Children.Add(new TextBlock { Text = "Settings", Style = (Style)Application.Current.Resources["TitleTextBlockStyle"] });
        titleStack.Children.Add(new TextBlock { Text = "Customize how Blaze looks, behaves and protects your browsing.", Foreground = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["TextFillColorSecondaryBrush"], TextWrapping = TextWrapping.Wrap });
        heading.Children.Add(titleStack);
        var close = new Button { Content = new FontIcon { Glyph = "\uE72B", FontFamily = new Microsoft.UI.Xaml.Media.FontFamily("Segoe Fluent Icons") }, Width = 40, Height = 40 };
        AutomationProperties.SetName(close, "Close settings");
        AutomationProperties.SetAutomationId(close, "SettingsBackButton");
        close.Click += (_, _) => CloseInternalPage();
        Grid.SetColumn(close, 1); heading.Children.Add(close); root.Children.Add(heading);
        Grid.SetRow(settingsTabs, 1); root.Children.Add(settingsTabs);
        var footer = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Spacing = 8 };
        var clearData = new Button { Content = "Clear browsing data" };
        AutomationProperties.SetAutomationId(clearData, "ClearBrowsingDataButton");
        clearData.Click += async (_, _) =>
        {
            var confirmation = new ContentDialog { Title = "Clear browsing data?", Content = "This removes cookies, caches, local storage, service workers, indexed databases and browsing history for the open WebKit profiles. You may be signed out of websites.", PrimaryButtonText = "Clear data", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Close, XamlRoot = XamlRoot };
            if (await ShowDialogAsync(confirmation) == ContentDialogResult.Primary)
            {
                var activeProfile = ViewModel.Tabs.FirstOrDefault(tab => tab.NativeHandle != IntPtr.Zero);
                if (activeProfile is not null) WebKitNative.ClearWebsiteData(activeProfile.NativeHandle);
                await ViewModel.ClearHistoryAsync();
            }
        };
        var cancel = new Button { Content = "Cancel" };
        cancel.Click += (_, _) => CloseInternalPage();
        var save = new Button { Content = "Save" };
        AutomationProperties.SetAutomationId(save, "SettingsSaveButton");
        save.Click += async (_, _) =>
        {
            ViewModel.Settings.StartupBehavior = startupBehavior.SelectedIndex == 0
                ? StartupBehavior.NewTab
                : StartupBehavior.ContinueSession;
            ViewModel.Settings.TabBarAboveToolbar = position.SelectedIndex == 0;
            ViewModel.Settings.HomePageCustomized = !string.IsNullOrWhiteSpace(homePage.Text);
            ViewModel.Settings.HomePage = ViewModel.Settings.HomePageCustomized
                ? homePage.Text.Trim()
                : BrowserSettings.NativeStartPageUrl;
            ViewModel.Settings.SearchEngine = (BrowserSearchEngine)Math.Clamp(searchEngine.SelectedIndex, 0, 3);
            ViewModel.Settings.JavaScriptEnabled = javaScript.IsOn;
            ViewModel.Settings.LoadImagesAutomatically = images.IsOn;
            ViewModel.Settings.ReduceMotion = reduceMotion.IsOn;
            ViewModel.Settings.PopupHandlingMode = popupHandling.SelectedIndex switch
            {
                1 => PopupHandlingMode.AlwaysAllow,
                2 => PopupHandlingMode.AlwaysDeny,
                _ => PopupHandlingMode.Ask
            };
            ApplyMotionPreferences();
            foreach (var control in permissionControls)
                ViewModel.ApplySitePermission(control.Site, control.Location.IsOn, control.Notifications.IsOn);
            foreach (var tab in ViewModel.Tabs)
            {
                if (tab.NativeHandle == IntPtr.Zero)
                    continue;
                WebKitNative.SetJavaScriptEnabled(tab.NativeHandle, javaScript.IsOn);
                WebKitNative.SetLoadsImagesAutomatically(tab.NativeHandle, images.IsOn);
            }
            ApplyTabBarPlacement();
            CloseInternalPage();
            await ViewModel.SaveAsync();
        };
        footer.Children.Add(clearData); footer.Children.Add(cancel); footer.Children.Add(save);
        Grid.SetRow(footer, 2); root.Children.Add(footer);
        OpenInternalPage("blaze://settings", "Settings", root);
    }

    private static void AddSettingsTab(TabView tabs, string title, string automationId, UIElement content)
    {
        var item = new TabViewItem
        {
            Header = title,
            Content = new ScrollViewer
            {
                Content = content,
                VerticalScrollMode = ScrollMode.Auto,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            },
            IsClosable = false
        };
        AutomationProperties.SetAutomationId(item, automationId);
        tabs.TabItems.Add(item);
    }

    private async Task HandlePermissionRequestAsync(string origin, int permissionType)
    {
        ViewModel.RecordPermissionRequest(origin, permissionType);
        var description = permissionType switch
        {
            0 => $"{origin} requested your location. The request was blocked. You can allow this site in Settings, then reload the page.",
            1 => $"{origin} requested permission to send notifications. The request was blocked. You can allow this site in Settings, then reload the page.",
            _ => $"{origin} requested camera or microphone access. It remains blocked because device selection is not yet implemented."
        };
        var dialog = new ContentDialog
        {
            Title = "Site permission blocked",
            Content = description,
            PrimaryButtonText = permissionType == 2 ? "Close" : "Manage permissions",
            CloseButtonText = "Dismiss",
            DefaultButton = ContentDialogButton.Close,
            XamlRoot = XamlRoot
        };
        try
        {
            if (await ShowDialogAsync(dialog) == ContentDialogResult.Primary && permissionType != 2)
                ShowSettingsPage();
        }
        catch (Exception ex)
        {
            ViewModel.StatusMessage = $"The permission dialog could not be displayed: {ex.Message}";
        }
    }

    private async Task HandleJavaScriptDialogAsync(int dialogType, string origin, string message, string defaultValue, IntPtr listener)
    {
        var kind = dialogType switch
        {
            0 => "alert",
            1 => "confirmation",
            2 => "prompt",
            _ => "navigation confirmation"
        };
        var content = new StackPanel { Spacing = 10 };
        if (!string.IsNullOrWhiteSpace(origin))
            content.Children.Add(new TextBlock { Text = origin, TextWrapping = TextWrapping.Wrap });
        content.Children.Add(new TextBlock { Text = message, TextWrapping = TextWrapping.Wrap });

        TextBox? responseBox = null;
        if (dialogType == 2)
        {
            responseBox = new TextBox { Header = "Response", Text = defaultValue };
            AutomationProperties.SetAutomationId(responseBox, "JavaScriptPromptInput");
            AutomationProperties.SetName(responseBox, "JavaScript prompt response");
            content.Children.Add(responseBox);
        }

        var isAlert = dialogType == 0;
        var dialog = new ContentDialog
        {
            Title = $"JavaScript {kind}",
            Content = new ScrollViewer { Content = content, MaxHeight = 620 },
            PrimaryButtonText = "OK",
            CloseButtonText = isAlert ? null : "Cancel",
            DefaultButton = isAlert ? ContentDialogButton.Primary : ContentDialogButton.Close,
            XamlRoot = XamlRoot
        };
        AutomationProperties.SetAutomationId(dialog, $"JavaScriptDialog_{kind.Replace(" ", "_")}");
        AutomationProperties.SetName(dialog, $"JavaScript {kind}");

        var accepted = isAlert;
        string? response = null;
        try
        {
            var result = await ShowDialogAsync(dialog);
            accepted = isAlert || result == ContentDialogResult.Primary;
            if (dialogType == 2 && accepted)
                response = responseBox?.Text ?? string.Empty;
        }
        catch
        {
            // Always release WebKit's suspended JavaScript execution, even if
            // the page is unloading or the native dialog cannot be displayed.
            accepted = isAlert;
        }
        finally
        {
            WebKitNative.ResolveJavaScriptDialog(listener, dialogType, accepted, response);
        }
    }

    private static void OnNativeJavaScriptDialog(int dialogType, string origin, string message, string defaultValue, IntPtr listener, IntPtr userData)
    {
        if (GCHandle.FromIntPtr(userData).Target is MainPage page
            && App.DispatcherQueue.TryEnqueue(() => _ = page.HandleJavaScriptDialogAsync(dialogType, origin, message, defaultValue, listener)))
            return;

        WebKitNative.ResolveJavaScriptDialog(listener, dialogType, dialogType == 0, null);
    }

    private static int OnNativeNewPage(IntPtr nativeHandle, IntPtr userData)
    {
        try
        {
            if (GCHandle.FromIntPtr(userData).Target is not MainPage page
                || !page._initialized || page._isPageUnloading)
                return 0;

            var mode = page.ViewModel.Settings.PopupHandlingMode;
            if (mode == PopupHandlingMode.AlwaysDeny)
                return 0;

            if (mode == PopupHandlingMode.Ask)
                return App.DispatcherQueue.TryEnqueue(() => page.QueuePopupPermission(nativeHandle)) ? 1 : 0;
            return App.DispatcherQueue.TryEnqueue(() => page.AttachPopupTab(nativeHandle)) ? 1 : 0;
        }
        catch
        {
            // Reject a popup if its owning WinUI page is no longer available.
            return 0;
        }
    }

    private static void OnNativePageClose(IntPtr nativeHandle, IntPtr userData)
    {
        try
        {
            if (GCHandle.FromIntPtr(userData).Target is MainPage page
                && page._nativeTabIds.TryGetValue(nativeHandle, out var tabId))
                App.DispatcherQueue.TryEnqueue(() => page.CloseNativeTab(nativeHandle, tabId));
        }
        catch
        {
            // The owning page may already be shutting down.
        }
    }

    private static void OnNativeDownload(string filePath, int status, long bytesWritten, long totalBytesExpected, IntPtr userData)
    {
        if (GCHandle.FromIntPtr(userData).Target is MainPage page)
            App.DispatcherQueue.TryEnqueue(() => page.ViewModel.UpdateDownload(filePath, status, bytesWritten, totalBytesExpected));
    }

    private static int OnNativePermission(string origin, int permissionType, IntPtr userData)
    {
        if (GCHandle.FromIntPtr(userData).Target is not MainPage page)
            return 0;
        var allowed = page.ViewModel.IsPermissionAllowed(origin, permissionType);
        if (!allowed)
            App.DispatcherQueue.TryEnqueue(() => _ = page.HandlePermissionRequestAsync(origin, permissionType));
        return allowed ? 1 : 0;
    }

    private void OnStateTimerTick(DispatcherQueueTimer sender, object args)
    {
        foreach (var openTab in ViewModel.Tabs)
        {
            if (openTab.NativeHandle == IntPtr.Zero || openTab.IsNativeStartPage)
                continue;

            openTab.RefreshNativeState();
            RequestTabFaviconIfNeeded(openTab);
            if (!string.Equals(openTab.LastRecordedUrl, openTab.Url, StringComparison.Ordinal))
            {
                ViewModel.RecordNavigation(openTab);
                _ = ViewModel.SaveAsync();
            }

            if (_tabHeaders.TryGetValue(openTab.Id, out var header))
            {
                header.Title.Text = openTab.Title.Length > 28 ? openTab.Title[..28] + "…" : openTab.Title;
                if (FindTabItem(openTab) is { } item)
                    AutomationProperties.SetName(item, openTab.Title);
            }
        }

        var tab = ViewModel.SelectedTab;
        if (tab is null)
            return;
        if (tab.IsNativeStartPage)
        {
            if (!_isAddressEditing && AddressBox.FocusState == FocusState.Unfocused)
                AddressBox.Text = string.Empty;
            UpdateBookmarkButton();
            UpdateNavigationButtons();
            return;
        }

        UpdateLoadingButton();
        if (!_isAddressEditing && AddressBox.FocusState == FocusState.Unfocused)
            AddressBox.Text = tab.Url;
        UpdateBookmarkButton();
        UpdateNavigationButtons();
    }

    private void NavigateTo(string url)
    {
        _isAddressEditing = false;
        DiscardSelectedInternalPage();
        ViewModel.NavigateAddressCommand.Execute(url);
        var tab = ViewModel.SelectedTab;
        AddressBox.Text = tab?.IsNativeStartPage == true ? string.Empty : ViewModel.AddressText;
        if (tab is not null && !tab.IsNativeStartPage)
            EnsureNativeView(tab);
        LayoutNativeViews();
        UpdateBookmarkButton();
        UpdateNavigationButtons();
        _ = ViewModel.SaveAsync();
    }

    private TabViewItem AddTabItem(BrowserTab tab)
    {
        var title = new TextBlock
        {
            Text = string.IsNullOrWhiteSpace(tab.Title) ? "New tab" : tab.Title,
            VerticalAlignment = VerticalAlignment.Center,
            TextTrimming = TextTrimming.CharacterEllipsis
        };
        var fallback = new FontIcon
        {
            Glyph = "\uE774",
            FontSize = 14,
            FontFamily = new Microsoft.UI.Xaml.Media.FontFamily("Segoe Fluent Icons"),
            VerticalAlignment = VerticalAlignment.Center
        };
        var favicon = new Image
        {
            Width = 16,
            Height = 16,
            Stretch = Microsoft.UI.Xaml.Media.Stretch.Uniform,
            Visibility = Visibility.Collapsed
        };
        ConfigureFaviconImage(favicon, fallback);
        var iconHost = new Grid { Width = 16, Height = 16 };
        iconHost.Children.Add(fallback);
        iconHost.Children.Add(favicon);

        title.MaxWidth = 220;
        var header = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 8,
            VerticalAlignment = VerticalAlignment.Center
        };
        // Let TabView size each tab from its content, with only a generous
        // title cap. This restores its normal adaptive tab-width behaviour.
        header.Children.Add(iconHost);
        header.Children.Add(title);

        var item = new TabViewItem
        {
            Header = header,
            IsClosable = true,
            Content = new Grid { Width = 0, Height = 0 }
        };
        _tabHeaders[tab.Id] = new TabHeaderVisuals(title, favicon, fallback);
        AutomationProperties.SetAutomationId(item, "BrowserTab_" + tab.Id);
        AutomationProperties.SetName(item, title.Text);
        _tabItems[item] = tab;
        TabStrip.TabItems.Add(item);
        UpdateClosableTabs();
        RequestTabFaviconIfNeeded(tab);
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
        if (tab.NativeHandle != IntPtr.Zero || !_initialized || tab.IsNativeStartPage
            || _internalPageRoots.ContainsKey(tab.Id) || IsInternalBrowserUrl(tab.Url))
            return;

        try
        {
            var (x, y, width, height) = GetBrowserSurfaceBounds();
            tab.NativeHandle = WebKitNative.Create(App.WindowHandle, x, y, Math.Max(1, width), Math.Max(1, height), tab.Url);
            if (tab.NativeHandle == IntPtr.Zero)
                throw new InvalidOperationException("WKViewCreate did not return a native view.");
            _nativeTabIds[tab.NativeHandle] = tab.Id;
            ConfigureNativeView(tab);
            CrashLogger.Log($"Created native WebKit view in owned popup host. Host HWND={WebKitNative.GetHostWindow(tab.NativeHandle):X}; child HWND={WebKitNative.GetWindow(tab.NativeHandle):X}; owner={App.WindowHandle:X}; bounds={x},{y},{width},{height}.");
            WebKitNative.SetJavaScriptEnabled(tab.NativeHandle, ViewModel.Settings.JavaScriptEnabled);
            WebKitNative.SetLoadsImagesAutomatically(tab.NativeHandle, ViewModel.Settings.LoadImagesAutomatically);
            WebKitNative.Navigate(tab.NativeHandle, tab.Url);
            _engineAvailable = true;
            EngineError.IsOpen = false;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException or InvalidOperationException or COMException)
        {
            if (tab.NativeHandle != IntPtr.Zero)
            {
                var failedHandle = tab.NativeHandle;
                tab.NativeHandle = IntPtr.Zero;
                _nativeTabIds.TryRemove(failedHandle, out _);
                WebKitNative.Destroy(failedHandle);
            }
            _engineAvailable = false;
            EngineError.Message = $"The pinned WinCairo runtime failed to initialize: {ex.Message}";
            EngineError.IsOpen = true;
            CrashLogger.LogException("Native WebKit view initialization failed.", ex);
        }
    }

    private void ConfigureNativeView(BrowserTab tab)
    {
        if (tab.NativeHandle == IntPtr.Zero)
            return;
        var callbackHandle = GCHandle.ToIntPtr(_nativeCallbackHandle);
        WebKitNative.SetDownloadCallback(tab.NativeHandle, DownloadCallbackHandler, callbackHandle);
        WebKitNative.SetPermissionCallback(tab.NativeHandle, PermissionCallbackHandler, callbackHandle);
        WebKitNative.SetJavaScriptDialogCallback(tab.NativeHandle, JavaScriptDialogCallbackHandler, callbackHandle);
        WebKitNative.SetPopupCallbacks(tab.NativeHandle, NewPageCallbackHandler, ClosePageCallbackHandler, callbackHandle);
        WebKitNative.SetUserAgent(tab.NativeHandle, SafariWindowsUserAgent);
    }

    private void DestroyNativeView(BrowserTab tab)
    {
        if (tab.NativeHandle == IntPtr.Zero)
            return;
        var nativeHandle = tab.NativeHandle;
        tab.NativeHandle = IntPtr.Zero;
        _nativeTabIds.TryRemove(nativeHandle, out _);
        DestroyNativeViewHandleImmediately(nativeHandle);
    }

    private void RetireNativeView(IntPtr nativeHandle)
    {
        if (nativeHandle == IntPtr.Zero)
            return;
        if (_isPageUnloading)
        {
            DestroyNativeViewHandleImmediately(nativeHandle);
            return;
        }
        if (!_retainedNativeViews.Add(nativeHandle))
            return;

        try
        {
            // Detach and hide immediately, but do not release WKView while the
            // WebKit GPU process may still have outstanding scene/compositing
            // work. Retain the native object until the browser page shuts down.
            WebKitNative.PrepareDestroy(nativeHandle);
            CrashLogger.Log($"Retired WebKit view 0x{nativeHandle:X}: hidden and callbacks detached; WKRelease deferred until application shutdown.");
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("Native WebKit view retirement failed; retaining the handle for shutdown.", exception);
        }
    }

    private void FinalizeNativeViewDisposal(IntPtr nativeHandle)
    {
        if (!_retainedNativeViews.Remove(nativeHandle))
            return;
        try
        {
            CrashLogger.Log($"Beginning shutdown-only native WebKit release for retired view 0x{nativeHandle:X}.");
            WebKitNative.Destroy(nativeHandle);
            CrashLogger.Log($"Finalized retained native WebKit view 0x{nativeHandle:X} during page shutdown.");
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("Native WebKit view final release failed.", exception);
        }
    }

    private static void DestroyNativeViewHandleImmediately(IntPtr nativeHandle)
    {
        if (nativeHandle == IntPtr.Zero)
            return;
        try
        {
            WebKitNative.PrepareDestroy(nativeHandle);
            WebKitNative.Destroy(nativeHandle);
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("Immediate native WebKit view destruction failed.", exception);
        }
    }

    private static bool IsInternalBrowserUrl(string? url) =>
        url?.StartsWith("blaze://", StringComparison.OrdinalIgnoreCase) == true;

    private void LayoutNativeViews()
    {
        if (!_initialized)
            return;

        var selected = ViewModel.SelectedTab;
        _internalPageOpen = selected is not null && _internalPageRoots.ContainsKey(selected.Id);
        var showStartPage = selected?.IsNativeStartPage == true && !_internalPageOpen;
        StartPageSurface.Visibility = showStartPage ? Visibility.Visible : Visibility.Collapsed;
        InternalPageSurface.Visibility = _internalPageOpen ? Visibility.Visible : Visibility.Collapsed;

        if (_internalPageOpen || showStartPage || EngineError.IsOpen || selected is null)
        {
            foreach (var tab in ViewModel.Tabs)
            {
                if (tab.NativeHandle != IntPtr.Zero)
                    WebKitNative.SetBounds(tab.NativeHandle, 0, 0, 0, 0);
            }
            return;
        }

        if (selected.NativeHandle == IntPtr.Zero)
            EnsureNativeView(selected);
        if (!_engineAvailable)
        {
            foreach (var tab in ViewModel.Tabs)
            {
                if (tab.NativeHandle != IntPtr.Zero)
                    WebKitNative.SetBounds(tab.NativeHandle, 0, 0, 0, 0);
            }
            return;
        }

        var (x, y, width, height) = GetBrowserSurfaceBounds();
        foreach (var tab in ViewModel.Tabs)
        {
            if (tab.NativeHandle != IntPtr.Zero && !ReferenceEquals(tab, selected))
                WebKitNative.SetBounds(tab.NativeHandle, 0, 0, 0, 0);
        }

        if (selected.NativeHandle != IntPtr.Zero
            && !WebKitNative.SetBounds(selected.NativeHandle, x, y, width, height))
        {
            _engineAvailable = false;
            EngineError.Message = "The native WebKit host window could not be positioned over the browser content area.";
            EngineError.IsOpen = true;
            CrashLogger.Log("Native WebKit host positioning failed; the browser content surface was hidden.");
            foreach (var tab in ViewModel.Tabs)
            {
                if (tab.NativeHandle != IntPtr.Zero)
                    WebKitNative.SetBounds(tab.NativeHandle, 0, 0, 0, 0);
            }
        }
    }

    private (int X, int Y, int Width, int Height) GetBrowserSurfaceBounds()
    {
        // Return physical-pixel coordinates relative to the main HWND's client
        // area. The native host converts them to screen coordinates for its
        // owned popup window, including the custom title-bar / Frame offsets.
        var root = App.Window.Content as UIElement;
        var transform = BrowserSurface.TransformToVisual(root);
        var point = transform.TransformPoint(new Point(0, 0));
        var scale = BrowserSurface.XamlRoot?.RasterizationScale ?? 1.0;
        var x = (int)Math.Round(point.X * scale);
        var y = (int)Math.Round(point.Y * scale);
        var width = (int)Math.Round(BrowserSurface.ActualWidth * scale);
        var height = (int)Math.Round(BrowserSurface.ActualHeight * scale);
        return (x, y, Math.Max(1, width), Math.Max(1, height));
    }

    private static string SiteAutomationToken(string origin) =>
        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(origin)))[..12];

    private void UpdateBookmarkButton()
    {
        var isBookmarked = ViewModel.IsBookmarked(ViewModel.SelectedTab?.Url);
        BookmarkButton.IsEnabled = ViewModel.SelectedTab?.IsNativeStartPage != true
            && !IsInternalBrowserUrl(ViewModel.SelectedTab?.Url);
        var accessibleName = isBookmarked ? "Remove bookmark" : "Add bookmark";
        AutomationProperties.SetName(BookmarkButton, accessibleName);
        ToolTipService.SetToolTip(BookmarkButton, accessibleName);
    }

    private void UpdateLoadingButton()
    {
        var isLoading = ViewModel.SelectedTab?.IsLoading == true;
        ReloadStopIcon.Glyph = isLoading ? "\uE71A" : "\uE72C";
        var accessibleName = isLoading ? "Stop loading" : "Reload page";
        AutomationProperties.SetName(ReloadStopButton, accessibleName);
        ToolTipService.SetToolTip(ReloadStopButton, isLoading ? "Stop loading" : "Reload");
    }

    private void UpdateLoadingProgress()
    {
        var tab = ViewModel.SelectedTab;
        if (tab is null || tab.IsNativeStartPage || !tab.IsLoading || tab.NativeHandle == IntPtr.Zero)
        {
            PageLoadingProgress.Value = 0;
            PageLoadingProgress.Visibility = Visibility.Collapsed;
            return;
        }

        var progress = WebKitNative.GetEstimatedProgress(tab.NativeHandle);
        PageLoadingProgress.Value = Math.Clamp(progress * 100.0, 0.0, 100.0);
        PageLoadingProgress.Visibility = Visibility.Visible;
    }

    private void UpdateNavigationButtons()
    {
        BackButton.IsEnabled = ViewModel.SelectedTab?.CanGoBack == true;
        ForwardButton.IsEnabled = ViewModel.SelectedTab?.CanGoForward == true;
        UpdateLoadingButton();
        UpdateLoadingProgress();
    }
}
