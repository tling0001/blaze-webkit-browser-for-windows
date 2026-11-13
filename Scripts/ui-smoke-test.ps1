param(
    [string]$ArtifactDirectory = ".\ui-test-artifacts"
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workflowId = [guid]::NewGuid().ToString('N')
$previousWorkflowId = $env:WINAPP_UI_WORKFLOW_ID
$env:WINAPP_UI_WORKFLOW_ID = $workflowId
$dotnetRunner = $null
$appProcess = $null
$smokePage = $null
$results = [Collections.Generic.List[object]]::new()
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null

function Invoke-WinAppChecked {
    param([string[]]$Arguments)
    $global:LASTEXITCODE = $null
    $output = @(& winapp @Arguments)
    $code = $global:LASTEXITCODE
    if ($null -eq $code -or $code -ne 0) {
        throw "winapp $($Arguments -join ' ') failed (exit $code). $($output -join [Environment]::NewLine)"
    }
    $output
}

try {
    $diagnosticLog = Join-Path $env:LOCALAPPDATA 'Blaze\Logs\blaze.log'
    if (Test-Path -LiteralPath $diagnosticLog -PathType Leaf) {
        Remove-Item -LiteralPath $diagnosticLog -Force
    }

    $stdoutPath = Join-Path $ArtifactDirectory 'dotnet-run.stdout.log'
    $stderrPath = Join-Path $ArtifactDirectory 'dotnet-run.stderr.log'
    $dotnetArgs = @(
        'run', '--project', 'WebKitBrowser.csproj', '--configuration', 'Release', '--no-restore',
        '--property:Platform=x64', '--property:RuntimeIdentifier=win-x64',
        '--property:PublishReadyToRun=false'
    )
    $dotnetRunner = Start-Process -FilePath 'dotnet' -WorkingDirectory $PWD -PassThru -ArgumentList $dotnetArgs -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (-not $dotnetRunner -or $dotnetRunner.Id -le 0) {
        throw 'Could not start Blaze through the Release dotnet run path.'
    }

    $appProcess = $null
    for ($attempt = 0; $attempt -lt 80; $attempt++) {
        $children = @(Get-CimInstance Win32_Process -Filter "Name = 'Blaze.exe' AND ParentProcessId = $($dotnetRunner.Id)" -ErrorAction SilentlyContinue)
        foreach ($child in $children) {
            $appProcess = Get-Process -Id ([int]$child.ProcessId) -ErrorAction SilentlyContinue
            if ($appProcess) { break }
        }
        if ($appProcess) { break }
        $dotnetRunner.Refresh()
        if ($dotnetRunner.HasExited) {
            $stdout = if (Test-Path $stdoutPath) { Get-Content $stdoutPath -Raw } else { '' }
            $stderr = if (Test-Path $stderrPath) { Get-Content $stderrPath -Raw } else { '' }
            throw "dotnet run exited with code $($dotnetRunner.ExitCode) before Blaze.exe started. stdout: $stdout stderr: $stderr"
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $appProcess) {
        throw 'dotnet run did not create the Blaze process within 20 seconds.'
    }
    $appProcessId = [int]$appProcess.Id
    $results.Add([ordered]@{ name = 'Release Blaze process started'; status = 'PASS'; detail = "PID $appProcessId" })

    $mainWindow = $null
    $lastWindowOutput = ''
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        $windowOutput = @(Invoke-WinAppChecked @('ui', 'list-windows', '--json', '-a', [string]$appProcessId))
        $lastWindowOutput = $windowOutput -join [Environment]::NewLine
        try { $windows = @($lastWindowOutput | ConvertFrom-Json) } catch { $windows = @() }
        $mainWindow = $windows | Where-Object {
            $_.hwnd -and $_.hwnd -ne '0' -and $_.title -like 'Blaze*'
        } | Select-Object -First 1
        if ($mainWindow) { break }
        if (-not (Get-Process -Id $appProcessId -ErrorAction SilentlyContinue)) {
            throw "Blaze exited before showing its main window. Last window enumeration: $lastWindowOutput"
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $mainWindow) {
        throw "Blaze remained running but did not expose its main window. Last window enumeration: $lastWindowOutput"
    }
    $results.Add([ordered]@{ name = 'WinUI main window appeared'; status = 'PASS'; detail = [string]$mainWindow.title })

    $screenshot = Join-Path $ArtifactDirectory 'blaze-startup.png'
    Invoke-WinAppChecked @('ui', 'screenshot', '--output', $screenshot, '-a', [string]$appProcessId) | Out-Null
    if (-not (Test-Path -LiteralPath $screenshot -PathType Leaf) -or (Get-Item -LiteralPath $screenshot).Length -eq 0) {
        throw 'The WinUI startup screenshot was not delivered.'
    }
    $results.Add([ordered]@{ name = 'Startup screenshot captured'; status = 'PASS'; detail = $screenshot })

    # Exercise native WinCairo with a local page (no network dependency).
    $smokePage = Join-Path $env:TEMP ("blaze-webkit-smoke-" + [guid]::NewGuid().ToString('N') + '.html')
    @'
<!doctype html>
<html><head><meta charset="utf-8"><title>Blaze WebKit Smoke</title></head>
<body><h1>Blaze WebKit Smoke</h1><p>Native WinCairo popup host is active.</p></body></html>
'@ | Set-Content -LiteralPath $smokePage -Encoding utf8
    $smokeUri = ([Uri]$smokePage).AbsoluteUri
    Invoke-WinAppChecked @('ui', 'send-keys', $smokeUri, '--target', 'AddressBox', '--via', 'send-input', '-a', [string]$appProcessId) | Out-Null
    Invoke-WinAppChecked @('ui', 'send-keys', 'enter', '--via', 'send-input', '-a', [string]$appProcessId) | Out-Null

    $nativeHostCreated = $false
    $pageTitleObserved = $false
    $hostHandle = [IntPtr]::Zero
    $childHandle = [IntPtr]::Zero
    $diagnosticLog = Join-Path $env:LOCALAPPDATA 'Blaze\Logs\blaze.log'
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if (Test-Path -LiteralPath $diagnosticLog -PathType Leaf) {
            $logText = Get-Content -LiteralPath $diagnosticLog -Raw
            if ($logText -match 'Created native WebKit view in owned popup host') {
                $nativeHostCreated = $true
            }
            if ($logText -match 'Host HWND=([0-9A-F]+); child HWND=([0-9A-F]+)') {
                $hostHandle = [IntPtr]([Convert]::ToInt64($matches[1], 16))
                $childHandle = [IntPtr]([Convert]::ToInt64($matches[2], 16))
            }
        }
        if ($nativeHostCreated) {
            $inspection = (Invoke-WinAppChecked @('ui', 'inspect', '--interactive', '--json', '-a', [string]$appProcessId)) -join [Environment]::NewLine
            if ($inspection -match 'Blaze WebKit Smoke') {
                $pageTitleObserved = $true
                break
            }
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $nativeHostCreated) {
        throw 'The local test page did not create its owned native WebKit popup host.'
    }
    $results.Add([ordered]@{ name = 'Native WebKit popup host created'; status = 'PASS' })
    if ($hostHandle -eq [IntPtr]::Zero -or $childHandle -eq [IntPtr]::Zero) {
        throw 'The native WebKit host or child HWND was not reported by the browser.'
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace BlazeSmoke {
    [StructLayout(LayoutKind.Sequential)]
    public struct NativePoint { public int X; public int Y; public NativePoint(int x, int y) { X = x; Y = y; } }
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeRect { public int Left; public int Top; public int Right; public int Bottom; }
    public static class Native {
        [DllImport("user32.dll", SetLastError = true)] public static extern bool GetWindowRect(IntPtr hwnd, out NativeRect rect);
        [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(NativePoint point);
        [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    }
}
'@
    if (-not [BlazeSmoke.Native]::IsWindowVisible($childHandle)) {
        throw 'The native WebKit child HWND is not visible after navigation.'
    }
    $hostRect = New-Object BlazeSmoke.NativeRect
    if (-not [BlazeSmoke.Native]::GetWindowRect($hostHandle, [ref]$hostRect)) {
        throw 'GetWindowRect failed for the native WebKit host window.'
    }
    $center = [BlazeSmoke.NativePoint]::new(
        [int](($hostRect.Left + $hostRect.Right) / 2),
        [int](($hostRect.Top + $hostRect.Bottom) / 2))
    $hitWindow = [BlazeSmoke.Native]::WindowFromPoint($center)
    $hitRoot = [BlazeSmoke.Native]::GetAncestor($hitWindow, 2) # GA_ROOT
    if ($hitRoot -ne $hostHandle) {
        throw ("The WebKit host is not the top hit-test window over the page area. Hit HWND: 0x{0:X}" -f $hitWindow.ToInt64())
    }
    $results.Add([ordered]@{ name = 'Native WebKit host is above the WinUI page surface'; status = 'PASS' })

    if (-not $pageTitleObserved) {
        throw 'The local WinCairo test page did not load or its document title did not reach the tab UI.'
    }
    $results.Add([ordered]@{ name = 'Local WebKit page title reached the tab'; status = 'PASS' })

    $contentScreenshot = Join-Path $ArtifactDirectory 'blaze-webkit-content.png'
    Invoke-WinAppChecked @('ui', 'screenshot', '--capture-screen', '--output', $contentScreenshot, '-a', [string]$appProcessId) | Out-Null
    if (-not (Test-Path -LiteralPath $contentScreenshot -PathType Leaf) -or (Get-Item -LiteralPath $contentScreenshot).Length -eq 0) {
        throw 'The native WebKit content screenshot was not delivered.'
    }
    $results.Add([ordered]@{ name = 'Native WebKit content screenshot captured'; status = 'PASS'; detail = $contentScreenshot })

    # Open Settings while WebKit content is loaded. The modal must be present
    # in the WinUI automation tree; the app hides its owned WebKit HWND first.
    Invoke-WinAppChecked @('ui', 'invoke', 'SettingsButton', '-a', [string]$appProcessId) | Out-Null
    Invoke-WinAppChecked @('ui', 'wait-for', 'SettingsGeneralTab', '-t', '5000', '-a', [string]$appProcessId) | Out-Null
    $settingsScreenshot = Join-Path $ArtifactDirectory 'blaze-settings-over-webkit.png'
    Invoke-WinAppChecked @('ui', 'screenshot', '--capture-screen', '--output', $settingsScreenshot, '-a', [string]$appProcessId) | Out-Null
    if (-not (Test-Path -LiteralPath $settingsScreenshot -PathType Leaf) -or (Get-Item -LiteralPath $settingsScreenshot).Length -eq 0) {
        throw 'The settings-over-WebKit screenshot was not delivered.'
    }
    $results.Add([ordered]@{ name = 'WinUI settings dialog opened above WebKit'; status = 'PASS'; detail = $settingsScreenshot })
} catch {
    $results.Add([ordered]@{ name = 'Launch smoke test'; status = 'FAIL'; detail = "$_" })
    Write-Warning $_
} finally {
    if ($smokePage -and (Test-Path -LiteralPath $smokePage)) {
        Remove-Item -LiteralPath $smokePage -Force -ErrorAction SilentlyContinue
    }
    if ($appProcess -and (Get-Process -Id $appProcess.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $appProcess.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $appProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    if ($dotnetRunner -and (Get-Process -Id $dotnetRunner.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $dotnetRunner.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $dotnetRunner.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    $diagnosticLog = Join-Path $env:LOCALAPPDATA 'Blaze\Logs\blaze.log'
    if (Test-Path -LiteralPath $diagnosticLog -PathType Leaf) {
        Copy-Item -LiteralPath $diagnosticLog -Destination (Join-Path $ArtifactDirectory 'blaze-diagnostic.log') -Force
    }
    try { Invoke-WinAppChecked @('ui', 'yield') | Out-Null } catch { }
    $env:WINAPP_UI_WORKFLOW_ID = $previousWorkflowId
}

$failed = @($results | Where-Object { $_.status -eq 'FAIL' }).Count
$report = [ordered]@{
    target = 'local'
    appPid = if ($appProcess) { $appProcess.Id } else { 0 }
    passed = $results.Count - $failed
    failed = $failed
    results = @($results.ToArray())
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath '.\ui-test-results.json' -Encoding utf8
Write-Host "Launch smoke test: $($report.passed) passed, $failed failed."
if ($failed -gt 0) { exit 1 }
