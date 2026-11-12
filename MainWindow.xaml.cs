using Microsoft.UI.Xaml;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using System.Runtime.InteropServices;
using Windows.Graphics;
using WebKitBrowser.Diagnostics;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace WebKitBrowser;

/// <summary>
/// The application window. This hosts a Frame that displays pages. Add your
/// UI and logic to MainPage.xaml / MainPage.xaml.cs instead of here so you
/// can use Page features such as navigation events and the Loaded lifecycle.
/// </summary>
public sealed partial class MainWindow : Window
{
    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hWnd);
    public MainWindow()
    {
        CrashLogger.Log("MainWindow constructor entered.");
        try
        {
            CrashLogger.Log("Initializing MainWindow XAML.");
            InitializeComponent();
            CrashLogger.Log("MainWindow XAML initialized.");

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
