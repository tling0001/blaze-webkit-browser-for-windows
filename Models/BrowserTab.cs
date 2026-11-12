using CommunityToolkit.Mvvm.ComponentModel;
using WebKitBrowser.Services;

namespace WebKitBrowser.Models;

public partial class BrowserTab : ObservableObject
{
    public string Id { get; } = Guid.NewGuid().ToString("N");

    [ObservableProperty]
    public partial string Title { get; set; }

    [ObservableProperty]
    public partial string Url { get; set; }

    [ObservableProperty]
    public partial bool CanGoBack { get; set; }

    [ObservableProperty]
    public partial bool CanGoForward { get; set; }

    public BrowserTab()
    {
        Title = "New tab";
        Url = "about:blank";
    }

    public bool IsNativeStartPage => string.Equals(Url, BrowserSettings.NativeStartPageUrl, StringComparison.OrdinalIgnoreCase);

    partial void OnUrlChanged(string value)
    {
        if (IsNativeStartPage)
        {
            Title = "Blaze";
            CanGoBack = false;
            CanGoForward = false;
        }
    }

    public IntPtr NativeHandle { get; set; }
    public string LastRecordedUrl { get; set; } = string.Empty;

    public void Navigate(string url)
    {
        Url = url;
        if (IsNativeStartPage)
        {
            Title = "Blaze";
            CanGoBack = false;
            CanGoForward = false;
            return;
        }
        if (NativeHandle != IntPtr.Zero)
            WebKitNative.Navigate(NativeHandle, url);
    }

    public void GoBack()
    {
        if (NativeHandle != IntPtr.Zero && CanGoBack)
            WebKitNative.GoBack(NativeHandle);
    }

    public void GoForward()
    {
        if (NativeHandle != IntPtr.Zero && CanGoForward)
            WebKitNative.GoForward(NativeHandle);
    }

    public void Reload()
    {
        if (NativeHandle != IntPtr.Zero)
            WebKitNative.Reload(NativeHandle);
    }

    public void Stop()
    {
        if (NativeHandle != IntPtr.Zero)
            WebKitNative.Stop(NativeHandle);
    }

    public void RefreshNativeState()
    {
        if (NativeHandle == IntPtr.Zero)
            return;

        CanGoBack = WebKitNative.CanGoBack(NativeHandle);
        CanGoForward = WebKitNative.CanGoForward(NativeHandle);
        Url = WebKitNative.GetCurrentUrl(NativeHandle) is { Length: > 0 } currentUrl ? currentUrl : Url;
        Title = WebKitNative.GetTitle(NativeHandle) is { Length: > 0 } title ? title : Url;
    }
}
