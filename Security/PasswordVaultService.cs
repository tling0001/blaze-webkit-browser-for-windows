using System.Runtime.InteropServices;
using Windows.Security.Credentials;

namespace WebKitBrowser.Security;

/// <summary>
/// Stores website credentials in the Windows per-user Credential Locker.
/// Password values are never written to Blaze's JSON profile files.
/// </summary>
internal static class PasswordVaultService
{
    private const string ResourcePrefix = "Blaze.Website.";

    internal sealed record SavedPassword(string Resource, string Origin, string UserName);

    private static PasswordVault CreateVault() => new();

    public static IReadOnlyList<SavedPassword> GetSavedPasswords()
    {
        try
        {
            return CreateVault().RetrieveAll()
                .Where(item => item.Resource.StartsWith(ResourcePrefix, StringComparison.Ordinal))
                .Select(item => new SavedPassword(item.Resource, item.Resource[ResourcePrefix.Length..], item.UserName))
                .OrderBy(item => item.Origin, StringComparer.OrdinalIgnoreCase)
                .ThenBy(item => item.UserName, StringComparer.OrdinalIgnoreCase)
                .ToArray();
        }
        catch (COMException exception) when ((uint)exception.HResult == 0x80070490)
        {
            return [];
        }
    }

    public static string NormalizeOrigin(string origin)
    {
        if (!Uri.TryCreate(origin, UriKind.Absolute, out var uri)
            || (uri.Scheme != Uri.UriSchemeHttps && uri.Scheme != Uri.UriSchemeHttp))
            throw new ArgumentException("Only http and https website origins can store credentials.", nameof(origin));
        return uri.GetLeftPart(UriPartial.Authority).TrimEnd('/');
    }

    public static IReadOnlyList<SavedPassword> FindForOrigin(string origin)
    {
        var normalized = NormalizeOrigin(origin);
        return GetSavedPasswords()
            .Where(item => string.Equals(item.Origin, normalized, StringComparison.OrdinalIgnoreCase))
            .ToArray();
    }

    public static void Save(string origin, string userName, string password)
    {
        if (string.IsNullOrWhiteSpace(userName))
            throw new ArgumentException("Enter a username.", nameof(userName));
        if (string.IsNullOrEmpty(password))
            throw new ArgumentException("Enter a password.", nameof(password));

        var normalizedOrigin = NormalizeOrigin(origin);
        var resource = ResourcePrefix + normalizedOrigin;
        var vault = CreateVault();
        try
        {
            foreach (var existing in vault.FindAllByResource(resource).Where(item =>
                string.Equals(item.UserName, userName.Trim(), StringComparison.OrdinalIgnoreCase)).ToArray())
                vault.Remove(existing);
        }
        catch (COMException exception) when ((uint)exception.HResult == 0x80070490)
        {
            // No matching resource exists yet; add the first credential below.
        }

        vault.Add(new PasswordCredential(resource, userName.Trim(), password));
    }

    public static string Retrieve(SavedPassword savedPassword)
    {
        var credential = CreateVault().Retrieve(savedPassword.Resource, savedPassword.UserName);
        credential.RetrievePassword();
        return credential.Password;
    }

    public static void Delete(SavedPassword savedPassword)
    {
        var vault = CreateVault();
        var credential = vault.Retrieve(savedPassword.Resource, savedPassword.UserName);
        vault.Remove(credential);
    }
}
