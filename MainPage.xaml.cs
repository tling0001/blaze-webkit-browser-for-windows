using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media.Animation;
using Windows.Foundation;
using Windows.UI.ViewManagement;
using WebKitBrowser.Models;
using WebKitBrowser.Services;
using WebKitBrowser.ViewModels;

namespace WebKitBrowser;

public sealed partial class MainPage : Page
{
    public MainPageViewModel ViewModel { get; } = new();

    private static readonly WebKitNative.DownloadCallback DownloadCallbackHandler = OnNativeDownload;
    private static readonly WebKitNative.PermissionCallback PermissionCallbackHandler = OnNativePermission;
    private static readonly WebKitNative.JavaScriptDialogCallback JavaScriptDialogCallbackHandler = OnNativeJavaScriptDialog;
    private static readonly WebKitNative.NewPageCallback NewPageCallbackHandler = OnNativeNewPage;
    private static readonly WebKitNative.ClosePageCallback ClosePageCallbackHandler = OnNativePageClose;

    private readonly Dictionary<TabViewItem, BrowserTab> _tabItems = [];
    private readonly HashSet<IntPtr> _pendingPopups = [];
    private readonly SemaphoreSlim _dialogGate = new(1, 1);
    private DispatcherQueueTimer? _stateTimer;
    private bool _initialized;
    private bool _isPageUnloading;
    private bool _engineAvailable;
    private GCHandle _nativeCallbackHandle;

    public MainPage()
    {
        InitializeComponent();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_initialized)
            return;
        _isPageUnloading = false;
        _initialized = true;

        await ViewModel.InitializeAsync();
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
        _stateTimer?.Stop();
        _stateTimer = null;
        _ = ViewModel.SaveAsync();
        foreach (var popup in _pendingPopups.ToArray())
            WebKitNative.Destroy(popup);
        _pendingPopups.Clear();
        foreach (var tab in ViewModel.Tabs)
            DestroyNativeView(tab);
    }

    private void OnAddTabButtonClick(TabView sender, TabViewAddTabButtonClickEventArgs args)
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
        if (args.Tab is TabViewItem item && _tabItems.TryGetValue(item, out var tab))
            CloseBrowserTab(tab);
    }

    private void CloseNativeTab(IntPtr nativeHandle)
    {
        if (nativeHandle == IntPtr.Zero)
            return;
        if (_pendingPopups.Remove(nativeHandle))
        {
            WebKitNative.Destroy(nativeHandle);
            return;
        }
        var tab = ViewModel.Tabs.FirstOrDefault(candidate => candidate.NativeHandle == nativeHandle);
        if (tab is not null)
            CloseBrowserTab(tab);
    }

    private void CloseBrowserTab(BrowserTab tab)
    {
        if (!ViewModel.Tabs.Contains(tab))
            return;

        var item = FindTabItem(tab);
        DestroyNativeView(tab);
        if (item is not null)
        {
            _tabItems.Remove(item);
            TabStrip.TabItems.Remove(item);
        }
        ViewModel.CloseTab(tab);

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

    private void AttachPopupTab(IntPtr nativeHandle)
    {
        if (nativeHandle == IntPtr.Zero)
            return;
        if (!_initialized || _isPageUnloading)
        {
            WebKitNative.Destroy(nativeHandle);
            return;
        }

        var tab = ViewModel.AddTab();
        tab.NativeHandle = nativeHandle;
        var item = AddTabItem(tab);
        TabStrip.SelectedItem = item;
        ConfigureNativeView(tab);
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
                WebKitNative.Destroy(nativeHandle);
        }
    }

    private void OnTabSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (TabStrip.SelectedItem is not TabViewItem item || !_tabItems.TryGetValue(item, out var tab))
            return;

        ViewModel.SelectedTab = tab;
        AddressBox.Text = tab.Url;
        UpdateBookmarkButton();
        EnsureNativeView(tab);
        LayoutNativeViews();
        UpdateNavigationButtons();
        _ = ViewModel.SaveAsync();
    }

    private void OnBrowserSurfaceSizeChanged(object sender, SizeChangedEventArgs e) => LayoutNativeViews();

    private void OnBackClick(object sender, RoutedEventArgs e) => ViewModel.GoBackCommand.Execute(null);
    private void OnForwardClick(object sender, RoutedEventArgs e) => ViewModel.GoForwardCommand.Execute(null);
    private void OnReloadClick(object sender, RoutedEventArgs e) => ViewModel.ReloadCommand.Execute(null);
    private void OnStopClick(object sender, RoutedEventArgs e) => ViewModel.StopCommand.Execute(null);

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
        BookmarkButton.Content = added ? "Bookmarked" : "Bookmark";
    }

    private void OnBookmarksClick(object sender, RoutedEventArgs e)
    {
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
        var flyout = CreateMenuFlyout();
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

    private async void OnSettingsClick(object sender, RoutedEventArgs e) => await ShowSettingsDialogAsync();

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
        BrowserSurface.Transitions = CreateEntranceTransitions();
    }

    private async Task<ContentDialogResult> ShowDialogAsync(ContentDialog dialog)
    {
        // ContentDialog is modal per XamlRoot. Serialize native dialogs so
        // simultaneous WebKit permission callbacks cannot throw or overlap.
        await _dialogGate.WaitAsync();
        try
        {
            dialog.Transitions = CreateEntranceTransitions();
            dialog.ContentTransitions = CreateEntranceTransitions();
            return await dialog.ShowAsync();
        }
        finally
        {
            _dialogGate.Release();
        }
    }

    private async Task ShowSettingsDialogAsync()
    {
        var position = new RadioButtons
        {
            Header = "Tab bar position",
            ItemsSource = new[] { "Above toolbar", "Below toolbar" },
            SelectedIndex = ViewModel.Settings.TabBarAboveToolbar ? 0 : 1
        };
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
        var panel = new StackPanel { Spacing = 12, MinWidth = 340 };
        panel.Children.Add(position);
        panel.Children.Add(javaScript);
        panel.Children.Add(images);
        panel.Children.Add(reduceMotion);
        panel.Children.Add(popupHandling);
        panel.Children.Add(new TextBlock
        {
            Text = "Automatic asks whether to allow each requested new window. Dismissing the prompt ignores that request.",
            TextWrapping = TextWrapping.Wrap
        });
        panel.Children.Add(new TextBlock
        {
            Text = "Site permissions",
            FontSize = 16,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold
        });

        var permissionControls = new List<(SitePermissionEntry Site, ToggleSwitch Location, ToggleSwitch Notifications)>();
        if (ViewModel.SitePermissions.Count == 0)
            panel.Children.Add(new TextBlock { Text = "No sites have requested location or notification access." });
        else
        {
            foreach (var site in ViewModel.SitePermissions)
            {
                panel.Children.Add(new TextBlock
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
                panel.Children.Add(location);
                panel.Children.Add(notifications);
            }
        }
        panel.Children.Add(new TextBlock
        {
            Text = "Camera and microphone requests are denied until a device-selection permission flow is available.",
            TextWrapping = TextWrapping.Wrap
        });

        var dialog = new ContentDialog
        {
            Title = "Settings",
            Content = new ScrollViewer { Content = panel, MaxHeight = 620 },
            PrimaryButtonText = "Save",
            SecondaryButtonText = "Clear browsing data",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = XamlRoot
        };

        var result = await ShowDialogAsync(dialog);
        if (result == ContentDialogResult.Secondary)
        {
            var confirmation = new ContentDialog
            {
                Title = "Clear browsing data?",
                Content = "This removes cookies, caches, local storage, service workers, indexed databases and browsing history for the open WebKit profiles. You may be signed out of websites.",
                PrimaryButtonText = "Clear data",
                CloseButtonText = "Cancel",
                DefaultButton = ContentDialogButton.Close,
                XamlRoot = XamlRoot
            };
            if (await ShowDialogAsync(confirmation) == ContentDialogResult.Primary)
            {
                var activeProfile = ViewModel.Tabs.FirstOrDefault(tab => tab.NativeHandle != IntPtr.Zero);
                if (activeProfile is not null)
                    WebKitNative.ClearWebsiteData(activeProfile.NativeHandle);
                await ViewModel.ClearHistoryAsync();
            }
            return;
        }
        if (result != ContentDialogResult.Primary)
            return;

        ViewModel.Settings.TabBarAboveToolbar = position.SelectedIndex == 0;
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
        LayoutNativeViews();
        await ViewModel.SaveAsync();
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
                await ShowSettingsDialogAsync();
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
            if (GCHandle.FromIntPtr(userData).Target is MainPage page)
                App.DispatcherQueue.TryEnqueue(() => page.CloseNativeTab(nativeHandle));
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
        var tab = ViewModel.SelectedTab;
        if (tab is null || tab.NativeHandle == IntPtr.Zero)
            return;

        tab.RefreshNativeState();
        if (AddressBox.FocusState == FocusState.Unfocused)
            AddressBox.Text = tab.Url;
        if (!string.Equals(tab.LastRecordedUrl, tab.Url, StringComparison.Ordinal))
        {
            ViewModel.RecordNavigation(tab);
            _ = ViewModel.SaveAsync();
        }

        if (FindTabItem(tab) is { } item)
        {
            item.Header = tab.Title.Length > 28 ? tab.Title[..28] + "…" : tab.Title;
            AutomationProperties.SetName(item, tab.Title);
        }
        UpdateBookmarkButton();
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
        AutomationProperties.SetAutomationId(item, "BrowserTab_" + tab.Id);
        AutomationProperties.SetName(item, item.Header?.ToString() ?? "New tab");
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
            ConfigureNativeView(tab);
            WebKitNative.SetJavaScriptEnabled(tab.NativeHandle, ViewModel.Settings.JavaScriptEnabled);
            WebKitNative.SetLoadsImagesAutomatically(tab.NativeHandle, ViewModel.Settings.LoadImagesAutomatically);
            WebKitNative.Navigate(tab.NativeHandle, tab.Url);
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

    private void ConfigureNativeView(BrowserTab tab)
    {
        if (tab.NativeHandle == IntPtr.Zero)
            return;
        var callbackHandle = GCHandle.ToIntPtr(_nativeCallbackHandle);
        WebKitNative.SetDownloadCallback(tab.NativeHandle, DownloadCallbackHandler, callbackHandle);
        WebKitNative.SetPermissionCallback(tab.NativeHandle, PermissionCallbackHandler, callbackHandle);
        WebKitNative.SetJavaScriptDialogCallback(tab.NativeHandle, JavaScriptDialogCallbackHandler, callbackHandle);
        WebKitNative.SetPopupCallbacks(tab.NativeHandle, NewPageCallbackHandler, ClosePageCallbackHandler, callbackHandle);
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

    private static string SiteAutomationToken(string origin) =>
        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(origin)))[..12];

    private void UpdateBookmarkButton()
    {
        BookmarkButton.Content = ViewModel.IsBookmarked(ViewModel.SelectedTab?.Url) ? "Bookmarked" : "Bookmark";
    }

    private void UpdateNavigationButtons()
    {
        BackButton.IsEnabled = ViewModel.SelectedTab?.CanGoBack == true;
        ForwardButton.IsEnabled = ViewModel.SelectedTab?.CanGoForward == true;
    }
}
