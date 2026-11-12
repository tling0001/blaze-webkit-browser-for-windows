using System.Text;

namespace WebKitBrowser.Diagnostics;

/// <summary>
/// Writes a local diagnostic trail for startup and unhandled exceptions.
/// Browser page contents, URLs, cookies and form data are deliberately never logged.
/// </summary>
internal static class CrashLogger
{
    private static readonly object Sync = new();
    private static bool _initialized;

    public static string LogDirectory => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Blaze", "Logs");

    public static string LogFilePath => Path.Combine(LogDirectory, "blaze.log");

    public static void Initialize()
    {
        lock (Sync)
        {
            if (_initialized) return;
            try
            {
                Directory.CreateDirectory(LogDirectory);
                RotateIfNeeded();
                _initialized = true;
                WriteLocked("INFO", $"Diagnostic logging started. Version={typeof(CrashLogger).Assembly.GetName().Version}; PID={Environment.ProcessId}; OS={Environment.OSVersion}; Runtime={Environment.Version}.");
            }
            catch { /* Diagnostics must never prevent the browser from starting. */ }
        }
    }

    public static void Log(string message) => Write("INFO", message);
    public static void LogWarning(string message) => Write("WARN", message);

    public static void LogException(string source, Exception exception)
    {
        ArgumentNullException.ThrowIfNull(exception);
        var details = new StringBuilder();
        for (Exception? current = exception; current is not null; current = current.InnerException)
        {
            details.AppendLine($"{current.GetType().FullName}: {current.Message} (HRESULT=0x{current.HResult:X8})");
            if (!string.IsNullOrWhiteSpace(current.StackTrace)) details.AppendLine(current.StackTrace);
        }
        Write("ERROR", $"{source}{Environment.NewLine}{details}");
    }

    public static void LogFatal(string source, object? exceptionObject)
    {
        var details = exceptionObject is Exception exception
            ? $"{exception.GetType().FullName}: {exception.Message} (HRESULT=0x{exception.HResult:X8}){Environment.NewLine}{exception}"
            : exceptionObject?.ToString() ?? "No exception object was supplied.";
        Write("FATAL", $"{source}{Environment.NewLine}{details}");
    }

    private static void Write(string level, string message)
    {
        lock (Sync)
        {
            try
            {
                if (!_initialized)
                {
                    Directory.CreateDirectory(LogDirectory);
                    _initialized = true;
                }
                WriteLocked(level, message);
            }
            catch { /* Logging is best-effort and must not recursively crash the app. */ }
        }
    }

    private static void WriteLocked(string level, string message)
    {
        using var stream = new FileStream(LogFilePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
        using var writer = new StreamWriter(stream, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        writer.WriteLine($"[{DateTimeOffset.Now:O}] [{level}] [PID {Environment.ProcessId}] {message}");
        writer.WriteLine();
        writer.Flush();
    }

    private static void RotateIfNeeded()
    {
        var path = LogFilePath;
        if (!File.Exists(path) || new FileInfo(path).Length < 2 * 1024 * 1024) return;
        var previous = Path.Combine(LogDirectory, "blaze.previous.log");
        if (File.Exists(previous)) File.Delete(previous);
        File.Move(path, previous);
    }
}
