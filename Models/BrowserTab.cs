using CommunityToolkit.Mvvm.ComponentModel;
using WebKitBrowser.Services;

namespace WebKitBrowser.Models;

public partial class BrowserTab : ObservableObject
{
    public string Id { get; } = Guid.NewGuid().ToString("N");

    [ObservableProperty]
    public partial string Title { get; set; } = "New tab";

    [ObservableProperty]
    public partial string Url { get; set; } = "about:blank";

    [ObservableProperty]
    public partial bool CanGoBack { get; set; }

    [ObservableProperty]
    public partial bool CanGoForward { get; set; }

    public IntPtr NativeHandle { get; set; }
    public string LastRecordedUrl { get; set; } = string.Empty;

    public void Navigate(string url)
    {
        Url = url;
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
