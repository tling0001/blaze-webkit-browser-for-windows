using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace WebKitBrowser.Diagnostics;

/// <summary>
/// Writes a privacy-conscious diagnostic trail for startup, XAML loading and unhandled failures.
/// URLs, page contents, cookies, form values and exception Data dictionaries are never logged.
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
                var app = Assembly.GetEntryAssembly();
                var xamlAssembly = typeof(Microsoft.UI.Xaml.Application).Assembly;
                WriteLocked("INFO",
                    $"Diagnostic logging started. ProductVersion={app?.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "unknown"}; " +
                    $"AssemblyVersion={app?.GetName().Version}; PID={Environment.ProcessId}; " +
                    $"OS={Environment.OSVersion}; OSArchitecture={RuntimeInformation.OSArchitecture}; " +
                    $"ProcessArchitecture={RuntimeInformation.ProcessArchitecture}; 64BitProcess={Environment.Is64BitProcess}; " +
                    $"Runtime={Environment.Version}; WindowsAppSDKAssembly={xamlAssembly.GetName().Version}; " +
                    $"BaseDirectory={AppContext.BaseDirectory}.");
            }
            catch { /* Diagnostics must never prevent the browser from starting. */ }
        }
    }

    public static void Log(string message) => Write("INFO", message);
    public static void LogWarning(string message) => Write("WARN", message);

    public static void LogXamlFailure(string xamlFile, Exception exception)
    {
        var details = new StringBuilder();
        details.AppendLine($"XAML resource load failed. Resource={xamlFile}.");
        AppendException(details, exception, 0, new HashSet<Exception>(ReferenceEqualityComparer.Instance));
        Write("ERROR", details.ToString());
    }

    public static void LogException(string source, Exception exception)
    {
        ArgumentNullException.ThrowIfNull(exception);
        var details = new StringBuilder();
        details.AppendLine(source);
        AppendException(details, exception, 0, new HashSet<Exception>(ReferenceEqualityComparer.Instance));
        Write("ERROR", details.ToString());
    }

    public static void LogFatal(string source, object? exceptionObject)
    {
        if (exceptionObject is Exception exception)
            LogException(source, exception);
        else
            Write("FATAL", $"{source}{Environment.NewLine}{exceptionObject?.GetType().FullName}: {exceptionObject ?? "No exception object was supplied."}");
    }

    private static void AppendException(StringBuilder details, Exception exception, int depth, HashSet<Exception> seen)
    {
        if (!seen.Add(exception))
        {
            details.AppendLine($"{new string(' ', depth * 2)}[Repeated exception reference]");
            return;
        }

        var indent = new string(' ', depth * 2);
        details.AppendLine($"{indent}ExceptionType={exception.GetType().FullName}");
        details.AppendLine($"{indent}Message={exception.Message}");
        details.AppendLine($"{indent}HRESULT=0x{exception.HResult:X8}");
        details.AppendLine($"{indent}Source={exception.Source ?? "(null)"}");
        details.AppendLine($"{indent}TargetSite={exception.TargetSite?.ToString() ?? "(null)"}");
        if (!string.IsNullOrWhiteSpace(exception.StackTrace))
            details.AppendLine($"{indent}StackTrace:{Environment.NewLine}{exception.StackTrace}");

        // XamlParseException exposes useful line metadata on some Windows App SDK builds.
        // Reflect only numeric diagnostic properties; never dump arbitrary properties or Data.
        foreach (var propertyName in new[] { "LineNumber", "LinePosition", "ErrorCode" })
        {
            try
            {
                var property = exception.GetType().GetProperty(propertyName, BindingFlags.Instance | BindingFlags.Public);
                if (property?.GetIndexParameters().Length == 0 &&
                    property.GetValue(exception) is IFormattable value)
                    details.AppendLine($"{indent}{propertyName}={value.ToString(null, System.Globalization.CultureInfo.InvariantCulture)}");
            }
            catch { }
        }

        if (exception is AggregateException aggregate)
        {
            for (var index = 0; index < aggregate.InnerExceptions.Count; index++)
            {
                details.AppendLine($"{indent}AggregateInnerException[{index}]:");
                AppendException(details, aggregate.InnerExceptions[index], depth + 1, seen);
            }
        }
        else if (exception.InnerException is not null)
        {
            details.AppendLine($"{indent}InnerException:");
            AppendException(details, exception.InnerException, depth + 1, seen);
        }

        if (exception is ReflectionTypeLoadException typeLoad)
        {
            for (var index = 0; index < typeLoad.LoaderExceptions.Length; index++)
            {
                if (typeLoad.LoaderExceptions[index] is { } loaderException)
                {
                    details.AppendLine($"{indent}LoaderException[{index}]:");
                    AppendException(details, loaderException, depth + 1, seen);
                }
            }
        }
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
                RotateIfNeeded();
                WriteLocked(level, message);
            }
            catch { /* Logging is best-effort and must not recursively crash the app. */ }
        }
    }

    private static void WriteLocked(string level, string message)
    {
        using var stream = new FileStream(LogFilePath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite, 4096, FileOptions.WriteThrough);
        using var writer = new StreamWriter(stream, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        writer.WriteLine($"[{DateTimeOffset.Now:O}] [{level}] [PID {Environment.ProcessId}] {message}");
        writer.WriteLine();
        writer.Flush();
        stream.Flush(flushToDisk: true);
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
