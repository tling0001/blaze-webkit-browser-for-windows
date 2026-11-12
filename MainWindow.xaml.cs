using Microsoft.UI.Xaml;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using System.Runtime.InteropServices;
using Windows.Graphics;
using WebKitBrowser.Diagnostics;

namespace WebKitBrowser;

public sealed partial class MainWindow : Window
{
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);

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
