param(
    [Parameter(Mandatory = $true)]
    [string]$Notes
)

$ErrorActionPreference = 'Stop'
$note = $Notes.Trim()
if ([string]::IsNullOrWhiteSpace($note)) { throw 'Release notes must not be empty.' }

$projectPath = Join-Path $PSScriptRoot '..\WebKitBrowser.csproj'
[xml]$project = Get-Content -LiteralPath $projectPath -Raw
$versionNode = $project.SelectSingleNode("/*[local-name()='Project']/*[local-name()='PropertyGroup']/*[local-name()='Version']")
if (-not $versionNode) { throw 'The project Version property was not found.' }
$current = $versionNode.InnerText.Trim()
$versionMatch = [regex]::Match($current, '^(?<base>\d{4}\.\d+\.\d+-port\.)(?<number>\d+)
$numericVersion = $next -replace '-port\.', '.'
$propertyGroup = $project.SelectSingleNode("/*[local-name()='Project']/*[local-name()='PropertyGroup']")
foreach ($pair in @(@('Version', $next), @('InformationalVersion', $next), @('AssemblyVersion', $numericVersion), @('FileVersion', $numericVersion), @('AppxPackageVersion', $numericVersion))) {
    $node = $project.SelectSingleNode("/*[local-name()='Project']/*[local-name()='PropertyGroup']/*[local-name()='$($pair[0])']")
    if (-not $node) { $node = $project.CreateElement($pair[0]); [void]$propertyGroup.AppendChild($node) }
    $node.InnerText = $pair[1]
}
$project.Save($projectPath)

$manifestPath = Join-Path $PSScriptRoot '..\Package.appxmanifest'
[xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
$identity = $manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Identity']")
if (-not $identity) { throw 'Package identity was not found in Package.appxmanifest.' }
$identity.SetAttribute('Version', $numericVersion)
$manifest.Save($manifestPath)

$notesPath = Join-Path $PSScriptRoot '..\RELEASE_NOTES.md'
$existing = Get-Content -LiteralPath $notesPath -Raw
$header = '# WebKit Browser release notes'
if (-not $existing.StartsWith($header)) { throw 'Unexpected RELEASE_NOTES.md format.' }
$today = Get-Date -Format 'yyyy-MM-dd'
$entry = "## [$next] - $today`n`n### Changes`n- $note`n`n"
Set-Content -LiteralPath $notesPath -Value ($header + "`n`n" + $entry + $existing.Substring($header.Length).TrimStart()) -NoNewline
Write-Host "Bumped version: $current -> $next"
)
if (-not $versionMatch.Success) { throw "Unsupported version format: $current" }
$next = "$($versionMatch.Groups['base'].Value)$([int]$versionMatch.Groups['number'].Value + 1)"
$numericVersion = $next -replace '-port\.', '.'
$propertyGroup = $project.SelectSingleNode("/*[local-name()='Project']/*[local-name()='PropertyGroup']")
foreach ($pair in @(@('Version', $next), @('InformationalVersion', $next), @('AssemblyVersion', $numericVersion), @('FileVersion', $numericVersion), @('AppxPackageVersion', $numericVersion))) {
    $node = $project.SelectSingleNode("/*[local-name()='Project']/*[local-name()='PropertyGroup']/*[local-name()='$($pair[0])']")
    if (-not $node) { $node = $project.CreateElement($pair[0]); [void]$propertyGroup.AppendChild($node) }
    $node.InnerText = $pair[1]
}
$project.Save($projectPath)

$manifestPath = Join-Path $PSScriptRoot '..\Package.appxmanifest'
[xml]$manifest = Get-Content -LiteralPath $manifestPath -Raw
$identity = $manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Identity']")
if (-not $identity) { throw 'Package identity was not found in Package.appxmanifest.' }
$identity.SetAttribute('Version', $numericVersion)
$manifest.Save($manifestPath)

$notesPath = Join-Path $PSScriptRoot '..\RELEASE_NOTES.md'
$existing = Get-Content -LiteralPath $notesPath -Raw
$header = '# WebKit Browser release notes'
if (-not $existing.StartsWith($header)) { throw 'Unexpected RELEASE_NOTES.md format.' }
$today = Get-Date -Format 'yyyy-MM-dd'
$entry = "## [$next] - $today`n`n### Changes`n- $note`n`n"
Set-Content -LiteralPath $notesPath -Value ($header + "`n`n" + $entry + $existing.Substring($header.Length).TrimStart()) -NoNewline
Write-Host "Bumped version: $current -> $next"
