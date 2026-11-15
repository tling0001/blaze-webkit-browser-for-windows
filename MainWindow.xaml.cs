using Microsoft.UI.Xaml;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using System.Runtime.InteropServices;
using Windows.Graphics;
using WebKitBrowser.Diagnostics;

namespace WebKitBrowser;

public sealed partial class MainWindow : Window
{
    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect { public int Left; public int Top; public int Right; public int Bottom; }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool GetWindowRect(IntPtr hwnd, out NativeRect rect);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")]
    private static extern bool IsZoomed(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int command);

    private const uint SwpNoZOrder = 0x0004;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpFrameChanged = 0x0020;
    private const int SwMaximize = 3;
    private NativeRect _windowedBounds;
    private bool _windowedWasMaximized;
    private bool _hasWindowedBounds;
    private bool _isBrowserFullscreen;

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);

    public void SetBrowserFullscreen(bool fullscreen)
    {
        if (_isBrowserFullscreen == fullscreen)
            return;
        var hwnd = Win32Interop.GetWindowFromWindowId(AppWindow.Id);
        if (fullscreen)
        {
            _hasWindowedBounds = GetWindowRect(hwnd, out _windowedBounds);
            _windowedWasMaximized = IsZoomed(hwnd);
            AppWindow.SetPresenter(AppWindowPresenterKind.FullScreen);
            AppTitleBar.Visibility = Visibility.Collapsed;
            _isBrowserFullscreen = true;
            return;
        }

        AppWindow.SetPresenter(AppWindowPresenterKind.Overlapped);
        AppTitleBar.Visibility = Visibility.Visible;
        if (_hasWindowedBounds)
        {
            if (_windowedWasMaximized)
                ShowWindow(hwnd, SwMaximize);
            else
                SetWindowPos(hwnd, IntPtr.Zero, _windowedBounds.Left, _windowedBounds.Top,
                    _windowedBounds.Right - _windowedBounds.Left, _windowedBounds.Bottom - _windowedBounds.Top,
                    SwpNoZOrder | SwpNoActivate | SwpFrameChanged);
        }
        _hasWindowedBounds = false;
        _isBrowserFullscreen = false;
    }

    public MainWindow()
    {
        CrashLogger.Log("MainWindow constructor entered.");
        try
        {
            CrashLogger.Log("Beginning MainWindow.xaml InitializeComponent.");
            try
            {
                InitializeComponent();
                CrashLogger.Log("MainWindow.xaml InitializeComponent completed.");
            }
            catch (Exception exception)
            {
                CrashLogger.LogXamlFailure("MainWindow.xaml", exception);
                throw;
            }

            ExtendsContentIntoTitleBar = true;
            SetTitleBar(AppTitleBar);
            CrashLogger.Log("Custom title bar configured.");

            AppWindow.SetIcon("Assets/AppIcon.ico");
            AppWindow.Title = "Blaze";
            CrashLogger.Log("Window icon and title configured.");

            var hwnd = Win32Interop.GetWindowFromWindowId(AppWindow.Id);
            var scale = GetDpiForWindow(hwnd) / 96.0;
            AppWindow.Resize(new SizeInt32((int)Math.Ceiling(1360 * scale), (int)Math.Ceiling(900 * scale)));
            CrashLogger.Log($"Initial window size configured. DPI scale={scale:0.###}.");

            RootFrame.Navigate(typeof(MainPage));
            CrashLogger.Log("RootFrame.Navigate(MainPage) completed.");
        }
        catch (Exception exception)
        {
            CrashLogger.LogException("MainWindow construction failed.", exception);
            throw;
        }
    }
}
