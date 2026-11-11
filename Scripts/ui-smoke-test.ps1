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
$dialogPage = Join-Path $env:TEMP "blaze-js-dialog-$workflowId.html"
$popupPage = Join-Path $env:TEMP "blaze-popup-$workflowId.html"

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

function Wait-ForPopupPolicy {
    param([int]$Expected)
    $settingsPath = Join-Path $env:LOCALAPPDATA 'Blaze\settings.json'
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        if (Test-Path -LiteralPath $settingsPath) {
            try {
                $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
                if ([int]$settings.PopupHandlingMode -eq $Expected) { return }
            } catch { }
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Popup policy $Expected was not persisted to settings.json."
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
    $main = $windows | Where-Object { $_.hwnd -and $_.hwnd -ne '0' -and $_.title -eq 'Blaze' } | Select-Object -First 1
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
        Invoke-Ui @('wait-for', 'AddressBox', '--value', 'https://example.com/', '-t', '15000') | Out-Null

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

    Test-UI 'WebKit window.open creates and closes a native browser tab' {
        @'
<!doctype html>
<html>
<head><meta charset="utf-8"><title>Blaze popup opener</title></head>
<body>
<script>
window.addEventListener('load', () => {
    const popup = window.open('about:blank', '_blank');
    if (!popup) {
        document.title = 'Popup blocked';
        return;
    }
    popup.document.open();
    popup.document.write('<!doctype html><title>Blaze popup passed</title><h1>Popup tab created</h1><script>setTimeout(() => window.close(), 6000);<\\/script>');
    popup.document.close();
});
</script>
<h1>Popup opener</h1>
</body>
</html>
'@ | Set-Content -LiteralPath $popupPage -Encoding utf8

        $popupUri = ([Uri]$popupPage).AbsoluteUri
        Invoke-Ui @('set-value', 'AddressBox', $popupUri) | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null
        Invoke-Ui @('wait-for', 'PopupPermissionDialog', '-t', '10000') | Out-Null
        Save-Screenshot '03-popup-permission.png'
        Invoke-Ui @('invoke', 'Primary') | Out-Null

        $popupTab = $null
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            $popupTab = $elements | Where-Object {
                $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Blaze popup passed'
            } | Select-Object -First 1
            if ($popupTab) { break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $popupTab) { throw 'window.open did not create a visible WinUI browser tab for the popup page.' }
        Save-Screenshot '04-webkit-popup-tab.png'
        Invoke-Ui @('wait-for', $popupTab.automationId, '--gone', '-t', '10000') | Out-Null
    }

    Test-UI 'Native settings dialog exposes and persists all popup policies' {
        Invoke-Ui @('invoke', 'SettingsButton') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '--value', 'Off', '-t', '5000') | Out-Null
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        foreach ($id in @('PopupHandlingMode', 'PopupModeAutomatic', 'PopupModeAlwaysAllow', 'PopupModeAlwaysDeny')) {
            if (-not ($elements | Where-Object { $_.automationId -eq $id })) {
                throw "The native Settings dialog did not expose $id."
            }
        }

        Invoke-Ui @('click', 'ReduceMotion') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '--value', 'On', '-t', '3000') | Out-Null
        Invoke-Ui @('click', 'PopupModeAlwaysDeny') | Out-Null
        Save-Screenshot '05-blaze-settings-dialog.png'
        Invoke-Ui @('invoke', 'Primary') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '--gone', '-t', '3000') | Out-Null

        $settingsPath = Join-Path $env:LOCALAPPDATA 'Blaze\settings.json'
        Wait-ForPopupPolicy 2

        Invoke-Ui @('invoke', 'SettingsButton') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '--value', 'On', '-t', '5000') | Out-Null
        Invoke-Ui @('click', 'PopupModeAlwaysAllow') | Out-Null
        Invoke-Ui @('invoke', 'Primary') | Out-Null
        Invoke-Ui @('wait-for', 'ReduceMotion', '--gone', '-t', '3000') | Out-Null
        Wait-ForPopupPolicy 1
    }

    Test-UI 'Home page and search engine settings persist and drive address searches' {
        $settingsPath = Join-Path $env:LOCALAPPDATA 'Blaze\settings.json'
        Invoke-Ui @('invoke', 'SettingsButton') | Out-Null
        Invoke-Ui @('wait-for', 'HomePage', '-t', '5000') | Out-Null
        Invoke-Ui @('set-value', 'HomePage', 'https://example.org') | Out-Null
        Invoke-Ui @('click', 'SearchEngineBing') | Out-Null
        Save-Screenshot '10-home-search-settings.png'
        Invoke-Ui @('invoke', 'Primary') | Out-Null
        Invoke-Ui @('wait-for', 'HomePage', '--gone', '-t', '3000') | Out-Null

        $saved = $false
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            if (Test-Path -LiteralPath $settingsPath) {
                try {
                    $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
                    if ($settings.HomePage -eq 'https://example.org' -and [int]$settings.SearchEngine -eq 1) {
                        $saved = $true
                        break
                    }
                } catch { }
            }
            Start-Sleep -Milliseconds 250
        }
        if (-not $saved) { throw 'The home page and Bing search preference were not persisted.' }

        Invoke-Ui @('invoke', 'HomeButton') | Out-Null
        Invoke-Ui @('wait-for', 'AddressBox', '--value', 'https://example.org/', '-t', '10000') | Out-Null
        Invoke-Ui @('set-value', 'AddressBox', 'Blaze search smoke test') | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null
        Invoke-Ui @('wait-for', 'AddressBox', '--value', 'https://www.bing.com/search?q=Blaze%20search%20smoke%20test', '-t', '5000') | Out-Null

        Invoke-Ui @('invoke', 'SettingsButton') | Out-Null
        Invoke-Ui @('wait-for', 'HomePage', '-t', '5000') | Out-Null
        Invoke-Ui @('set-value', 'HomePage', 'https://www.google.com') | Out-Null
        Invoke-Ui @('click', 'SearchEngineGoogle') | Out-Null
        Invoke-Ui @('invoke', 'Primary') | Out-Null
        Invoke-Ui @('wait-for', 'HomePage', '--gone', '-t', '3000') | Out-Null
    }

    Test-UI 'Always deny rejects a pop-up without creating a tab' {
        $popupUri = ([Uri]$popupPage).AbsoluteUri
        Invoke-Ui @('set-value', 'AddressBox', $popupUri) | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null

        $blockedTab = $null
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            $blockedTab = $elements | Where-Object {
                $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Popup blocked'
            } | Select-Object -First 1
            if ($blockedTab) { break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $blockedTab) { throw 'Always deny did not return null from window.open and update the opener title.' }
        if ($elements | Where-Object { $_.automationId -eq 'PopupPermissionDialog' }) {
            throw 'Always deny displayed a permission prompt instead of rejecting the pop-up.'
        }
    }

    Test-UI 'Always allow opens a pop-up without prompting' {
        $popupUri = ([Uri]$popupPage).AbsoluteUri
        Invoke-Ui @('set-value', 'AddressBox', $popupUri) | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null

        $popupTab = $null
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            if ($elements | Where-Object { $_.automationId -eq 'PopupPermissionDialog' }) {
                throw 'Always allow unexpectedly displayed a permission prompt.'
            }
            $popupTab = $elements | Where-Object {
                $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Blaze popup passed'
            } | Select-Object -First 1
            if ($popupTab) { break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $popupTab) { throw 'Always allow did not create a visible native browser tab.' }
        Save-Screenshot '09-popup-always-allow.png'
        Invoke-Ui @('wait-for', $popupTab.automationId, '--gone', '-t', '10000') | Out-Null

        Invoke-Ui @('invoke', 'SettingsButton') | Out-Null
        Invoke-Ui @('wait-for', 'PopupHandlingMode', '-t', '5000') | Out-Null
        Invoke-Ui @('click', 'PopupModeAutomatic') | Out-Null
        Invoke-Ui @('invoke', 'Primary') | Out-Null
        $settingsPath = Join-Path $env:LOCALAPPDATA 'Blaze\settings.json'
        Wait-ForPopupPolicy 0
    }


    Test-UI 'Ignore dismisses the popup request without opening a tab' {
        $popupUri = ([Uri]$popupPage).AbsoluteUri
        Invoke-Ui @('set-value', 'AddressBox', $popupUri) | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null
        Invoke-Ui @('wait-for', 'PopupPermissionDialog', '-t', '10000') | Out-Null
        $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
        $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
        if ($elements | Where-Object { $_.automationId -eq 'Secondary' }) { throw 'A separate secondary popup action is still exposed.' }
        if (-not ($elements | Where-Object { $_.automationId -eq 'Close' })) { throw 'The single Ignore dismissal button was not exposed.' }
        Invoke-Ui @('invoke', 'Close') | Out-Null
        Invoke-Ui @('wait-for', 'PopupPermissionDialog', '--gone', '-t', '5000') | Out-Null
        $blocked = $false
        for ($attempt = 0; $attempt -lt 10; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            if ($elements | Where-Object { $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Popup blocked' }) { $blocked = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $blocked) { throw 'Ignoring the popup did not leave the opener in its blocked-popup state.' }
    }

    Test-UI 'WebKit JavaScript alert, confirm and prompt use native WinUI dialogs' {
        @'
<!doctype html>
<html>
<head><meta charset="utf-8"><title>Blaze dialog test</title></head>
<body>
<script>
window.addEventListener('load', () => {
    alert('Blaze alert test');
    if (confirm('Blaze confirmation test')) {
        const value = prompt('Blaze prompt test', 'initial value');
        document.body.dataset.dialogResult = value || '';
        document.title = value || 'empty response';
    }
});
</script>
<h1>JavaScript dialog test</h1>
</body>
</html>
'@ | Set-Content -LiteralPath $dialogPage -Encoding utf8

        $dialogUri = ([Uri]$dialogPage).AbsoluteUri
        Invoke-Ui @('set-value', 'AddressBox', $dialogUri) | Out-Null
        Invoke-Ui @('send-keys', 'enter', '--via', 'send-input') | Out-Null

        Invoke-Ui @('wait-for', 'JavaScriptDialog_alert', '-t', '15000') | Out-Null
        Save-Screenshot '06-javascript-alert.png'
        Invoke-Ui @('invoke', 'Primary') | Out-Null

        Invoke-Ui @('wait-for', 'JavaScriptDialog_confirmation', '-t', '10000') | Out-Null
        Save-Screenshot '07-javascript-confirm.png'
        Invoke-Ui @('invoke', 'Primary') | Out-Null

        Invoke-Ui @('wait-for', 'JavaScriptDialog_prompt', '-t', '10000') | Out-Null
        Invoke-Ui @('set-value', 'JavaScriptPromptInput', 'Blaze prompt passed') | Out-Null
        Save-Screenshot '08-javascript-prompt.png'
        Invoke-Ui @('invoke', 'Primary') | Out-Null
        Invoke-Ui @('wait-for', 'JavaScriptDialog_prompt', '--gone', '-t', '5000') | Out-Null
        $promptResultFound = $false
        for ($attempt = 0; $attempt -lt 10; $attempt++) {
            $inspection = Invoke-Ui @('inspect', '--interactive', '--json') | ConvertFrom-Json
            $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
            if ($elements | Where-Object { $_.automationId -like 'BrowserTab_*' -and $_.name -eq 'Blaze prompt passed' }) {
                $promptResultFound = $true
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (-not $promptResultFound) { throw 'The WebKit prompt did not receive the response entered in the native WinUI TextBox.' }
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
        if ($dialogPage -and (Test-Path -LiteralPath $dialogPage)) { Remove-Item -LiteralPath $dialogPage -Force }
        if ($popupPage -and (Test-Path -LiteralPath $popupPage)) { Remove-Item -LiteralPath $popupPage -Force }
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
