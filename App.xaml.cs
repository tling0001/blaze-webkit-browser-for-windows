using Microsoft.UI.Xaml;
using WebKitBrowser.Diagnostics;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace WebKitBrowser;

/// <summary>
/// Provides application-specific behavior to supplement the default Application class.
/// </summary>
public partial class App : Application
{
    /// <summary>
    /// The main application window. Use <c>App.Window</c> from any class that needs
    /// the window reference (for dialogs, pickers, interop, etc.).
    /// </summary>
    public static Window Window { get; private set; } = null!;

    /// <summary>
    /// The UI thread dispatcher. Use <c>App.DispatcherQueue</c> to marshal calls
    /// to the UI thread. Fully qualified to avoid CS0104 ambiguity with
    /// <see cref="Windows.System.DispatcherQueue"/>.
    /// </summary>
    public static Microsoft.UI.Dispatching.DispatcherQueue DispatcherQueue { get; private set; } = null!;

    /// <summary>
    /// The native window handle (HWND). Use for file pickers,
    /// <c>DataTransferManager</c>, and any WinRT interop that requires
    /// <c>InitializeWithWindow</c>.
    /// </summary>
    public static nint WindowHandle =>
        WinRT.Interop.WindowNative.GetWindowHandle(Window);

    /// <summary>
    /// Initializes the singleton application object.
    /// </summary>
    public App()
    {
        CrashLogger.Initialize();
        CrashLogger.Log("App constructor entered.");
        UnhandledException += OnUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;
        TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
        try { CrashLogger.Log("Initializing application XAML resources."); InitializeComponent(); CrashLogger.Log("Application XAML resources initialized."); }
        catch (Exception exception) { CrashLogger.LogException("Application construction failed.", exception); throw; }
    }

    private void OnUnhandledException(object sender, Microsoft.UI.Xaml.UnhandledExceptionEventArgs e) => CrashLogger.LogFatal("WinUI unhandled exception.", e.Exception);
    private static void OnDomainUnhandledException(object sender, System.UnhandledExceptionEventArgs e) => CrashLogger.LogFatal($"AppDomain unhandled exception. IsTerminating={e.IsTerminating}.", e.ExceptionObject);
    private static void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e) { CrashLogger.LogException("Unobserved task exception.", e.Exception); e.SetObserved(); }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override void OnLaunched(Microsoft.UI.Xaml.LaunchActivatedEventArgs args)
    {
        CrashLogger.Log("Application launch activation entered.");
        try { Window = new MainWindow(); CrashLogger.Log("MainWindow constructed."); DispatcherQueue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread(); Window.Activate(); CrashLogger.Log("MainWindow activated."); }
        catch (Exception exception) { CrashLogger.LogException("Application launch failed.", exception); throw; }
    }
}
