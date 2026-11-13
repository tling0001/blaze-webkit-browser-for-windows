using System.Collections.Concurrent;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace WebKitBrowser.Services;

/// <summary>
/// Retrieves site-declared icons and keeps a bounded, expiring local cache.
/// This disposable UI cache is separate from WebKit cookies and website storage.
/// </summary>
internal sealed class FaviconCache
{
    private const int MaximumHtmlBytes = 2 * 1024 * 1024;
    private const int MaximumIconBytes = 1024 * 1024;
    private static readonly TimeSpan MaximumAge = TimeSpan.FromDays(30);
    private static readonly HttpClient Client = CreateClient();

    private readonly string? _cacheDirectory;
    private readonly ConcurrentDictionary<string, Task<string?>> _pending = new(StringComparer.OrdinalIgnoreCase);

    public FaviconCache()
    {
        var preferred = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Blaze", "Temp", "Favicons");
        try
        {
            Directory.CreateDirectory(preferred);
            _cacheDirectory = preferred;
        }
        catch
        {
            var fallback = Path.Combine(Path.GetTempPath(), "Blaze", "Favicons");
            try
            {
                Directory.CreateDirectory(fallback);
                _cacheDirectory = fallback;
            }
            catch
            {
                _cacheDirectory = null;
            }
        }
        PruneCache();
    }

    public Task<string?> GetFaviconPathAsync(string pageUrl)
    {
        if (!Uri.TryCreate(pageUrl, UriKind.Absolute, out var pageUri)
            || (pageUri.Scheme != Uri.UriSchemeHttp && pageUri.Scheme != Uri.UriSchemeHttps))
            return Task.FromResult<string?>(null);

        var origin = pageUri.GetLeftPart(UriPartial.Authority) + "/";
        var key = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(origin))).ToLowerInvariant();
        var cached = FindCachedIcon(key);
        if (cached is not null)
            return Task.FromResult<string?>(cached);

        var pending = _pending.GetOrAdd(key, _ => DownloadIconAsync(new Uri(origin), key));
        return AwaitAndForgetAsync(key, pending);
    }

    private async Task<string?> AwaitAndForgetAsync(string key, Task<string?> pending)
    {
        try { return await pending; }
        finally { _pending.TryRemove(key, out _); }
    }

    private static HttpClient CreateClient()
    {
        var handler = new HttpClientHandler
        {
            AllowAutoRedirect = true,
            AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate | DecompressionMethods.Brotli
        };
        var client = new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(10) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/27.2 Safari/605.1.15");
        client.DefaultRequestHeaders.Accept.ParseAdd("text/html,application/xhtml+xml,image/*,*/*;q=0.8");
        return client;
    }

    private string? FindCachedIcon(string key)
    {
        if (_cacheDirectory is null)
            return null;
        try
        {
            foreach (var path in Directory.EnumerateFiles(_cacheDirectory, key + ".*"))
            {
                var info = new FileInfo(path);
                if (DateTime.UtcNow - info.LastWriteTimeUtc <= MaximumAge && info.Length > 0)
                {
                    info.LastAccessTimeUtc = DateTime.UtcNow;
                    return path;
                }
                info.Delete();
            }
        }
        catch
        {
            // A cache read failure falls back to downloading the site icon.
        }
        return null;
    }

    private async Task<string?> DownloadIconAsync(Uri origin, string key)
    {
        try
        {
            using var pageResponse = await Client.GetAsync(origin, HttpCompletionOption.ResponseHeadersRead);
            if (!pageResponse.IsSuccessStatusCode)
                return await TryDownloadIconAsync(new Uri(origin, "favicon.ico"), key);

            var htmlBytes = await ReadBoundedAsync(pageResponse.Content, MaximumHtmlBytes);
            if (htmlBytes is null)
                return await TryDownloadIconAsync(new Uri(origin, "favicon.ico"), key);

            var html = Encoding.UTF8.GetString(htmlBytes);
            var baseUri = pageResponse.RequestMessage?.RequestUri ?? origin;
            foreach (var candidate in FindIconUris(html, baseUri))
            {
                var downloaded = await TryDownloadIconAsync(candidate, key);
                if (downloaded is not null)
                    return downloaded;
            }

            return await TryDownloadIconAsync(new Uri(baseUri.GetLeftPart(UriPartial.Authority) + "/favicon.ico"), key);
        }
        catch
        {
            return null;
        }
    }

    private static IEnumerable<Uri> FindIconUris(string html, Uri baseUri)
    {
        var candidates = new List<(Uri Uri, int Score)>();
        foreach (Match tagMatch in Regex.Matches(html, @"<link\b[^>]*>", RegexOptions.IgnoreCase | RegexOptions.Singleline))
        {
            var tag = tagMatch.Value;
            var rel = GetAttribute(tag, "rel") ?? string.Empty;
            var href = WebUtility.HtmlDecode(GetAttribute(tag, "href") ?? string.Empty).Trim();
            if (string.IsNullOrWhiteSpace(href) || !rel.Contains("icon", StringComparison.OrdinalIgnoreCase)
                || href.StartsWith("data:", StringComparison.OrdinalIgnoreCase)
                || !Uri.TryCreate(baseUri, href, out var iconUri)
                || (iconUri.Scheme != Uri.UriSchemeHttp && iconUri.Scheme != Uri.UriSchemeHttps))
                continue;

            var score = rel.Equals("icon", StringComparison.OrdinalIgnoreCase) ? 100 : 80;
            var sizes = GetAttribute(tag, "sizes");
            if (!string.IsNullOrWhiteSpace(sizes))
            {
                var dimension = Regex.Match(sizes, @"(?<size>\d+)x\d+", RegexOptions.IgnoreCase);
                if (dimension.Success && int.TryParse(dimension.Groups["size"].Value, out var size))
                    score += Math.Min(size, 128) / 4;
            }
            candidates.Add((iconUri, score));
        }

        return candidates.OrderByDescending(candidate => candidate.Score)
            .Select(candidate => candidate.Uri)
            .Distinct();
    }

    private static string? GetAttribute(string tag, string name)
    {
        var match = Regex.Match(tag,
            "\\b" + Regex.Escape(name) + "\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))",
            RegexOptions.IgnoreCase | RegexOptions.Singleline);
        if (!match.Success)
            return null;
        for (var group = 1; group <= 3; group++)
        {
            if (match.Groups[group].Success)
                return match.Groups[group].Value;
        }
        return null;
    }

    private async Task<string?> TryDownloadIconAsync(Uri iconUri, string key)
    {
        if (_cacheDirectory is null)
            return null;
        try
        {
            using var response = await Client.GetAsync(iconUri, HttpCompletionOption.ResponseHeadersRead);
            if (!response.IsSuccessStatusCode)
                return null;
            var bytes = await ReadBoundedAsync(response.Content, MaximumIconBytes);
            if (bytes is null)
                return null;

            var extension = DetectImageExtension(bytes);
            if (extension is null)
                return null;

            var path = Path.Combine(_cacheDirectory, key + extension);
            await File.WriteAllBytesAsync(path, bytes);
            File.SetLastWriteTimeUtc(path, DateTime.UtcNow);
            return path;
        }
        catch
        {
            return null;
        }
    }

    private static async Task<byte[]?> ReadBoundedAsync(HttpContent content, int maximumBytes)
    {
        if (content.Headers.ContentLength is long length && length > maximumBytes)
            return null;

        await using var stream = await content.ReadAsStreamAsync();
        using var buffer = new MemoryStream();
        var chunk = new byte[8192];
        while (true)
        {
            var read = await stream.ReadAsync(chunk.AsMemory(0, chunk.Length));
            if (read == 0)
                break;
            if (buffer.Length + read > maximumBytes)
                return null;
            buffer.Write(chunk, 0, read);
        }
        return buffer.ToArray();
    }

    private static string? DetectImageExtension(byte[] bytes)
    {
        if (bytes.Length >= 8 && bytes.AsSpan(0, 8).SequenceEqual(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }))
            return ".png";
        if (bytes.Length >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1 && bytes[3] == 0)
            return ".ico";
        if (bytes.Length >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
            return ".jpg";
        if (bytes.Length >= 6 && Encoding.ASCII.GetString(bytes, 0, 4) == "GIF8")
            return ".gif";
        if (bytes.Length >= 2 && bytes[0] == 0x42 && bytes[1] == 0x4D)
            return ".bmp";
        if (bytes.Length > 0)
        {
            var prefix = Encoding.UTF8.GetString(bytes, 0, Math.Min(bytes.Length, 1024));
            if (prefix.Contains("<svg", StringComparison.OrdinalIgnoreCase))
                return ".svg";
        }
        return null;
    }

    private void PruneCache()
    {
        if (_cacheDirectory is null)
            return;
        try
        {
            var files = new DirectoryInfo(_cacheDirectory).GetFiles()
                .OrderByDescending(file => file.LastAccessTimeUtc)
                .ToList();
            foreach (var file in files.Where(file => DateTime.UtcNow - file.LastWriteTimeUtc > MaximumAge).ToList())
                file.Delete();

            files = new DirectoryInfo(_cacheDirectory).GetFiles()
                .OrderByDescending(file => file.LastAccessTimeUtc)
                .ToList();
            foreach (var file in files.Skip(256))
                file.Delete();
        }
        catch
        {
            // Cache cleanup must never prevent the browser from launching.
        }
    }
}
