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

$today = Get-Date -Format 'yyyy-MM-dd'
$note = $Notes.Trim()
if ([string]::IsNullOrWhiteSpace($note)) { throw 'Release notes must not be empty.' }
$entry = "## [$next] - $today`n`n### Changes`n- $note`n`n"
$existing = Get-Content -LiteralPath $notesPath -Raw
$header = '# WebKit Browser release notes'
if (-not $existing.StartsWith($header)) { throw 'Unexpected RELEASE_NOTES.md format.' }
Set-Content -LiteralPath $notesPath -Value ($header + "`n`n" + $entry + $existing.Substring($header.Length).TrimStart()) -NoNewline
Write-Host "Bumped version: $current -> $next"
