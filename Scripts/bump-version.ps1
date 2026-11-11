param(
    [Parameter(Mandatory = $true)]
    [string]$Notes
)

$ErrorActionPreference = 'Stop'
$versionPath = Join-Path $PSScriptRoot '..\VERSION'
$notesPath = Join-Path $PSScriptRoot '..\RELEASE_NOTES.md'
$current = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ($current -notmatch '^(?<base>\d{4}\.\d+\.\d+-port\.)(?<number>\d+)$') {
    throw "Unsupported version format: $current"
}
$next = "$($Matches.base)$([int]$Matches.number + 1)"
Set-Content -LiteralPath $versionPath -Value $next -NoNewline

$projectPath = Join-Path $PSScriptRoot '..\WebKitBrowser.csproj'
[xml]$project = Get-Content -LiteralPath $projectPath -Raw
$numericVersion = $next -replace '-port\.', '.'
$propertyGroup = $project.Project.PropertyGroup | Select-Object -First 1
foreach ($pair in @(@('Version', $next), @('InformationalVersion', $next), @('AssemblyVersion', $numericVersion), @('FileVersion', $numericVersion), @('AppxPackageVersion', $numericVersion))) {
    $node = $project.SelectSingleNode("/Project/PropertyGroup/*[local-name()='$($pair[0])']")
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

$today = Get-Date -Format 'yyyy-MM-dd'
$note = $Notes.Trim()
if ([string]::IsNullOrWhiteSpace($note)) { throw 'Release notes must not be empty.' }
$entry = "## [$next] - $today`n`n### Changes`n- $note`n`n"
$existing = Get-Content -LiteralPath $notesPath -Raw
$header = '# WebKit Browser release notes'
if (-not $existing.StartsWith($header)) { throw 'Unexpected RELEASE_NOTES.md format.' }
Set-Content -LiteralPath $notesPath -Value ($header + "`n`n" + $entry + $existing.Substring($header.Length).TrimStart()) -NoNewline
Write-Host "Bumped version: $current -> $next"
