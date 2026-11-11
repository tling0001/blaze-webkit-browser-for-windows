using System.Runtime.InteropServices;
using System.Text;

namespace WebKitBrowser.Services;

/// <summary>
/// Narrow C ABI over the upstream WinCairo WKView API. The host and WebKit
/// runtime are required; this class deliberately has no WebView2 fallback.
/// </summary>
internal static class WebKitNative
{
    private const string Library = "WebKitHost.dll";

    [DllImport(Library, EntryPoint = "WB_Create", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern IntPtr Create(IntPtr parent, int x, int y, int width, int height, string initialUrl);

    [DllImport(Library, EntryPoint = "WB_Destroy", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void Destroy(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_SetBounds", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetBounds(IntPtr view, int x, int y, int width, int height);

    [DllImport(Library, EntryPoint = "WB_Navigate", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern void Navigate(IntPtr view, string url);

    [DllImport(Library, EntryPoint = "WB_GoBack", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void GoBack(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_GoForward", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void GoForward(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_Reload", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void Reload(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_Stop", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void Stop(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_CanGoBack", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool CanGoBack(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_CanGoForward", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool CanGoForward(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_GetCurrentUrl", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int GetCurrentUrlNative(IntPtr view, StringBuilder buffer, int capacity);

    [DllImport(Library, EntryPoint = "WB_GetTitle", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int GetTitleNative(IntPtr view, StringBuilder buffer, int capacity);

    [DllImport(Library, EntryPoint = "WB_SetJavaScriptEnabled", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetJavaScriptEnabled(IntPtr view, [MarshalAs(UnmanagedType.Bool)] bool enabled);

    [DllImport(Library, EntryPoint = "WB_SetLoadsImagesAutomatically", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetLoadsImagesAutomatically(IntPtr view, [MarshalAs(UnmanagedType.Bool)] bool enabled);

    [DllImport(Library, EntryPoint = "WB_ClearWebsiteData", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ClearWebsiteData(IntPtr view);

    internal static string GetCurrentUrl(IntPtr view) => ReadString(GetCurrentUrlNative, view);
    internal static string GetTitle(IntPtr view) => ReadString(GetTitleNative, view);

    private delegate int StringReader(IntPtr view, StringBuilder buffer, int capacity);

    private static string ReadString(StringReader reader, IntPtr view)
    {
        var buffer = new StringBuilder(4096);
        var length = reader(view, buffer, buffer.Capacity);
        return length <= 0 ? string.Empty : buffer.ToString();
    }
}
