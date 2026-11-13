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
    Invoke-WinAppChecked @('ui', 'screenshot', '--output', $screenshot, '-w', [string]$mainWindow.hwnd) | Out-Null
    if (-not (Test-Path -LiteralPath $screenshot -PathType Leaf) -or (Get-Item -LiteralPath $screenshot).Length -eq 0) {
        throw 'The WinUI startup screenshot was not delivered.'
    }
    $results.Add([ordered]@{ name = 'Startup screenshot captured'; status = 'PASS'; detail = $screenshot })
} catch {
    $results.Add([ordered]@{ name = 'Launch smoke test'; status = 'FAIL'; detail = "$_" })
    Write-Warning $_
} finally {
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
