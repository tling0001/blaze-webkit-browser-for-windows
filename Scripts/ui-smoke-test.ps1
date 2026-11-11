param(
    [string]$ArtifactDirectory = ".\ui-test-artifacts"
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$workflowId = [guid]::NewGuid().ToString('N')
$previousWorkflowId = $env:WINAPP_UI_WORKFLOW_ID
$env:WINAPP_UI_WORKFLOW_ID = $workflowId
$appPid = 0
$results = [Collections.Generic.List[object]]::new()
$screenshots = [Collections.Generic.List[string]]::new()
$uiStarted = $false

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

function Invoke-Ui {
    param([string[]]$Arguments, [string]$Window)
    $selector = if ($Window) { @('-w', $Window) } else { @('-a', "$appPid") }
    Invoke-WinAppChecked (@('ui') + $Arguments + $selector)
}

function Get-Elements {
    param($Nodes)
    foreach ($node in $Nodes) {
        if ($null -ne $node) {
            $node
            if ($node.children) { Get-Elements $node.children }
        }
    }
}

function Save-Screenshot {
    param([string]$Name)
    $path = Join-Path $ArtifactDirectory $Name
    Invoke-Ui @('screenshot', '--output', $path) -Window $hwnd | Out-Null
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Screenshot was not delivered: $path"
    }
    $screenshots.Add([IO.Path]::GetFullPath($path))
}

function Test-UI {
    param([string]$Name, [scriptblock]$Body)
    try {
        & $Body | Out-Null
        $results.Add(@{ name = $Name; status = 'PASS' })
    } catch {
        $results.Add(@{ name = $Name; status = 'FAIL'; detail = "$_" })
        Write-Warning "$Name failed: $_"
    }
}

try {
    $ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
    New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null

    $launch = (Invoke-WinAppChecked @('run', '.', '--detach', '--json') | ConvertFrom-Json)
    if ($launch.Error -or -not $launch.ProcessId -or [int]$launch.ProcessId -le 0 -or ($launch.ProcessScope -and $launch.ProcessScope -ne 'local')) {
        throw "WinApp CLI did not return an app process ID. $($launch.Error)"
    }
    $appPid = [int]$launch.ProcessId
    $uiStarted = $true

    $windows = @(Invoke-Ui @('list-windows', '--json') | ConvertFrom-Json)
    $main = $windows | Where-Object { $_.hwnd -and $_.hwnd -ne '0' -and $_.title -match '^Blaze } | Select-Object -First 1
    if (-not $main) { throw 'The launched WinUI browser window was not found.' }
    $hwnd = [string]$main.hwnd

    Test-UI 'WinUI browser window and address control' {
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        $address = $elements | Where-Object { $_.automationId -eq 'AddressBox' } | Select-Object -First 1
        if (-not $address) { throw 'AddressBox AutomationId was not found in the WinUI tree.' }
        Save-Screenshot '01-browser-start.png'
    }

    Test-UI 'Live WebKit navigation to HTTPS example.com' {
        Invoke-Ui @('set-value', 'AddressBox', 'https://example.com') | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null
        Invoke-Ui @('wait-for', 'AddressBox', '--value', 'https://example.com', '-t', '15000') | Out-Null

        $foundTitle = $false
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            $pageTab = $elements | Where-Object {
                $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Example Domain'
            } | Select-Object -First 1
            if ($pageTab) { $foundTitle = $true; break }
            Start-Sleep -Seconds 1
        }
        if (-not $foundTitle) { throw 'The WebKit page title "Example Domain" did not appear in the browser tab.' }
        Save-Screenshot '02-example-domain.png'
    }

    Test-UI 'Native settings dialog opens with reduced-motion control' {
        Invoke-Ui @('invoke', 'SettingsButton') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '-t', '5000') | Out-Null
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        if (-not ($elements | Where-Object { $_.automationId -eq 'ReduceMotion' })) {
            throw 'The native Settings ContentDialog did not expose the ReduceMotion ToggleSwitch.'
        }
        Save-Screenshot '03-blaze-settings-dialog.png'
        Invoke-Ui @('invoke', 'Close') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '--gone', '-t', '3000') | Out-Null
    }

    Test-UI 'Tab and settings controls expose AutomationIds' {
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        foreach ($id in @('BrowserTabStrip', 'BackButton', 'ForwardButton', 'ReloadButton', 'StopButton', 'HomeButton', 'BookmarkButton', 'BookmarksButton', 'HistoryButton', 'DownloadsButton', 'SettingsButton')) {
            if (-not ($elements | Where-Object { $_.automationId -eq $id })) {
                throw "Missing AutomationId: $id"
            }
        }
    }
} catch {
    $results.Add(@{ name = 'UI smoke setup'; status = 'FAIL'; detail = "$_" })
    Write-Warning "UI smoke setup failed: $_"
} finally {
    try {
        if ($uiStarted) { Invoke-WinAppChecked @('ui', 'yield') | Out-Null }
    } catch {
        $results.Add(@{ name = 'Release UI workflow'; status = 'FAIL'; detail = "$_" })
    } finally {
        $env:WINAPP_UI_WORKFLOW_ID = $previousWorkflowId
    }
}

$failed = @($results | Where-Object { $_.status -eq 'FAIL' }).Count
$report = [ordered]@{
    target = 'local'
    appPid = $appPid
    workflowId = $workflowId
    passed = $results.Count - $failed
    failed = $failed
    results = @($results.ToArray())
    screenshots = @($screenshots.ToArray())
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath '.\ui-test-results.json' -Encoding utf8
Write-Host "Passed: $($report.passed) | Failed: $failed | Results: .\ui-test-results.json"
if ($failed -gt 0) { exit 1 }
 } | Select-Object -First 1
    if (-not $main) { throw 'The launched WinUI browser window was not found.' }
    $hwnd = [string]$main.hwnd

    Test-UI 'WinUI browser window and address control' {
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        $address = $elements | Where-Object { $_.automationId -eq 'AddressBox' } | Select-Object -First 1
        if (-not $address) { throw 'AddressBox AutomationId was not found in the WinUI tree.' }
        Save-Screenshot '01-browser-start.png'
    }

    Test-UI 'Live WebKit navigation to HTTPS example.com' {
        Invoke-Ui @('set-value', 'AddressBox', 'https://example.com') | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null
        Invoke-Ui @('wait-for', 'AddressBox', '--value', 'https://example.com', '-t', '15000') | Out-Null

        $foundTitle = $false
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            $pageTab = $elements | Where-Object {
                $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Example Domain'
            } | Select-Object -First 1
            if ($pageTab) { $foundTitle = $true; break }
            Start-Sleep -Seconds 1
        }
        if (-not $foundTitle) { throw 'The WebKit page title "Example Domain" did not appear in the browser tab.' }
        Save-Screenshot '02-example-domain.png'
    }

    Test-UI 'Tab and settings controls expose AutomationIds' {
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        foreach ($id in @('BrowserTabStrip', 'BackButton', 'ForwardButton', 'ReloadButton', 'StopButton', 'HomeButton', 'BookmarkButton', 'BookmarksButton', 'HistoryButton', 'DownloadsButton', 'SettingsButton')) {
            if (-not ($elements | Where-Object { $_.automationId -eq $id })) {
                throw "Missing AutomationId: $id"
            }
        }
    }
} catch {
    $results.Add(@{ name = 'UI smoke setup'; status = 'FAIL'; detail = "$_" })
    Write-Warning "UI smoke setup failed: $_"
} finally {
    try {
        if ($uiStarted) { Invoke-WinAppChecked @('ui', 'yield') | Out-Null }
    } catch {
        $results.Add(@{ name = 'Release UI workflow'; status = 'FAIL'; detail = "$_" })
    } finally {
        $env:WINAPP_UI_WORKFLOW_ID = $previousWorkflowId
    }
}

$failed = @($results | Where-Object { $_.status -eq 'FAIL' }).Count
$report = [ordered]@{
    target = 'local'
    appPid = $appPid
    workflowId = $workflowId
    passed = $results.Count - $failed
    failed = $failed
    results = @($results.ToArray())
    screenshots = @($screenshots.ToArray())
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath '.\ui-test-results.json' -Encoding utf8
Write-Host "Passed: $($report.passed) | Failed: $failed | Results: .\ui-test-results.json"
if ($failed -gt 0) { exit 1 }
