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

    [DllImport(Library, EntryPoint = "WB_PrepareDestroy", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void PrepareDestroy(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_Destroy", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void Destroy(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_SetBounds", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool SetBounds(IntPtr view, int x, int y, int width, int height);

    [DllImport(Library, EntryPoint = "WB_GetWindow", CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr GetWindow(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_GetHostWindow", CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr GetHostWindow(IntPtr view);

    [DllImport(Library, EntryPoint = "WB_Navigate", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern void Navigate(IntPtr view, string url);

    [DllImport(Library, EntryPoint = "WB_SetUserAgent", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern void SetUserAgent(IntPtr view, string userAgent);

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

    [DllImport(Library, EntryPoint = "WB_GetEstimatedProgress", CallingConvention = CallingConvention.Cdecl)]
    internal static extern double GetEstimatedProgress(IntPtr view);

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

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal delegate void DownloadCallback([MarshalAs(UnmanagedType.LPWStr)] string filePath, int status, long bytesWritten, long totalBytesExpected, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal delegate int PermissionCallback([MarshalAs(UnmanagedType.LPWStr)] string origin, int permissionType, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal delegate void JavaScriptDialogCallback(int dialogType, [MarshalAs(UnmanagedType.LPWStr)] string origin, [MarshalAs(UnmanagedType.LPWStr)] string message, [MarshalAs(UnmanagedType.LPWStr)] string defaultValue, IntPtr listener, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int NewPageCallback(IntPtr view, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ClosePageCallback(IntPtr view, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void FullscreenCallback(IntPtr view, [MarshalAs(UnmanagedType.Bool)] bool isFullscreen, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_SetDownloadCallback", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetDownloadCallback(IntPtr view, DownloadCallback callback, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_SetPermissionCallback", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetPermissionCallback(IntPtr view, PermissionCallback callback, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_SetJavaScriptDialogCallback", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetJavaScriptDialogCallback(IntPtr view, JavaScriptDialogCallback callback, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal delegate void CredentialBridgeCallback(IntPtr view, [MarshalAs(UnmanagedType.LPWStr)] string origin, [MarshalAs(UnmanagedType.LPWStr)] string payload, IntPtr listener, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_SetCredentialBridgeCallback", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetCredentialBridgeCallback(IntPtr view, CredentialBridgeCallback callback, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_EvaluateJavaScript", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern void EvaluateJavaScript(IntPtr view, string script);

    [DllImport(Library, EntryPoint = "WB_SetPopupCallbacks", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetPopupCallbacks(IntPtr view, NewPageCallback newPageCallback, ClosePageCallback closePageCallback, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_SetFullscreenCallback", CallingConvention = CallingConvention.Cdecl)]
    internal static extern void SetFullscreenCallback(IntPtr view, FullscreenCallback callback, IntPtr userData);

    [DllImport(Library, EntryPoint = "WB_ResolveJavaScriptDialog", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    internal static extern void ResolveJavaScriptDialog(IntPtr listener, int dialogType, [MarshalAs(UnmanagedType.Bool)] bool accepted, string? response);

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


/// <summary>
/// Opt-in D3D11-to-WinUI composition surface. The WebKit GPU process supplies
/// validated FrameReadyMessage values after the per-view descriptor IPC lands.
/// </summary>
internal sealed class GpuCompositionSurface : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct AdapterLuid
    {
        public uint LowPart;
        public int HighPart;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SharedTextureSlotDescriptor
    {
        public ulong SharedResourceToken;
        public ulong ProducerAcquireKey;
        public ulong ConsumerAcquireKey;
        public ulong ConsumerReturnKey;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SharedSurfaceDescriptor
    {
        public uint StructSize;
        public uint ProtocolVersion;
        public ulong ViewIdentifier;
        public ulong ResizeGeneration;
        public uint Width;
        public uint Height;
        public uint PixelFormat;
        public uint SynchronizationKind;
        public uint SlotCount;
        public uint Reserved;
        public AdapterLuid Adapter;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public SharedTextureSlotDescriptor[] Slots;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FrameReadyMessage
    {
        public uint StructSize;
        public uint ProtocolVersion;
        public ulong ViewIdentifier;
        public ulong ResizeGeneration;
        public ulong FrameSequence;
        public uint SlotIndex;
        public uint Reserved;
        public ulong ProducerReleaseKey;
        public ulong ConsumerReleaseKey;
        public ulong ProducerQpc;
    }

    private const string Library = "WebKitHost.dll";
    private IntPtr _handle;
    private readonly ulong _viewIdentifier;
    private ulong _generation;
    private uint _width;
    private uint _height;

    [DllImport(Library, EntryPoint = "WB_GpuSurfaceCreate", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr CreateNative(ulong viewIdentifier, uint width, uint height, ulong generation);
    [DllImport(Library, EntryPoint = "WB_GpuSurfaceGetDescriptor", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetDescriptorNative(IntPtr surface, IntPtr descriptor);
    [DllImport(Library, EntryPoint = "WB_GpuSurfaceBindToWebKitView", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool BindToWebKitViewNative(IntPtr view, IntPtr surface);
    [DllImport(Library, EntryPoint = "WB_GpuSurfaceAttachSwapChainPanel", CallingConvention = CallingConvention.Cdecl)]
    private static extern int AttachNative(IntPtr surface, IntPtr panelUnknown);
    [DllImport(Library, EntryPoint = "WB_GpuSurfacePresent", CallingConvention = CallingConvention.Cdecl)]
    private static extern int PresentNative(IntPtr surface, in FrameReadyMessage frame);
    [DllImport(Library, EntryPoint = "WB_GpuSurfaceDetachSwapChainPanel", CallingConvention = CallingConvention.Cdecl)]
    private static extern int DetachNative(IntPtr surface);
    [DllImport(Library, EntryPoint = "WB_GpuSurfaceDestroy", CallingConvention = CallingConvention.Cdecl)]
    private static extern void DestroyNative(IntPtr surface);

    internal GpuCompositionSurface(ulong viewIdentifier, uint width, uint height, ulong generation)
    {
        _viewIdentifier = viewIdentifier;
        _generation = generation;
        _width = width;
        _height = height;
        _handle = CreateNative(viewIdentifier, width, height, generation);
        if (_handle == IntPtr.Zero)
            throw new InvalidOperationException("The native D3D11 shared-texture ring could not be created.");
    }

    internal SharedSurfaceDescriptor GetDescriptor()
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        var size = Marshal.SizeOf<SharedSurfaceDescriptor>();
        if (size != 152)
            throw new InvalidOperationException($"Shared-surface ABI size mismatch: expected 152 bytes, got {size}.");
        var buffer = Marshal.AllocHGlobal(size);
        try
        {
            if (!GetDescriptorNative(_handle, buffer))
                throw new InvalidOperationException("The native D3D11 shared-surface descriptor is invalid.");
            return Marshal.PtrToStructure<SharedSurfaceDescriptor>(buffer);
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    internal void BindToWebKitView(IntPtr nativeView)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        if (nativeView == IntPtr.Zero || !BindToWebKitViewNative(nativeView, _handle))
            throw new InvalidOperationException("The shared D3D11 surface descriptor could not be forwarded to the WebKit GPU process.");
    }

    internal void ResizeAndBind(IntPtr nativeView, uint width, uint height)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        if (nativeView == IntPtr.Zero)
            throw new ArgumentException("A live WebKit view is required.", nameof(nativeView));
        if (width == _width && height == _height)
            return;

        var nextGeneration = checked(_generation + 1);
        var nextHandle = CreateNative(_viewIdentifier, width, height, nextGeneration);
        if (nextHandle == IntPtr.Zero)
            throw new InvalidOperationException("The resized D3D11 shared-texture ring could not be created.");
        try
        {
            // The current GPU-process receiver only stores descriptors. Once texture import is enabled,
            // replace this with an explicit GPU-process acknowledgement before releasing the old ring.
            if (!BindToWebKitViewNative(nativeView, nextHandle))
                throw new InvalidOperationException("The resized D3D11 descriptor could not be submitted to WebKit.");
        }
        catch
        {
            DestroyNative(nextHandle);
            throw;
        }

        var previousHandle = Interlocked.Exchange(ref _handle, nextHandle);
        DestroyNative(previousHandle);
        _generation = nextGeneration;
        _width = width;
        _height = height;
    }

    internal void Attach(Microsoft.UI.Xaml.Controls.SwapChainPanel panel)
    {
        ArgumentNullException.ThrowIfNull(panel);
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        var unknown = Marshal.GetIUnknownForObject(panel);
        try { Marshal.ThrowExceptionForHR(AttachNative(_handle, unknown)); }
        finally { Marshal.Release(unknown); }
    }

    internal void Present(in FrameReadyMessage frame)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        Marshal.ThrowExceptionForHR(PresentNative(_handle, frame));
    }

    internal void Detach()
    {
        if (_handle != IntPtr.Zero)
            Marshal.ThrowExceptionForHR(DetachNative(_handle));
    }

    public void Dispose()
    {
        var handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle == IntPtr.Zero)
            return;
        DestroyNative(handle);
        GC.SuppressFinalize(this);
    }
}
