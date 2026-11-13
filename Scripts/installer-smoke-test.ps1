param(
    [Parameter(Mandatory = $true)][string]$InstallerPath,
    [string]$ArtifactDirectory = ".\\installer-test-artifacts"
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
$installDirectory = Join-Path $env:LOCALAPPDATA 'Programs\\Blaze'
$executable = Join-Path $installDirectory 'Blaze.exe'
$diagnosticLog = Join-Path $env:LOCALAPPDATA 'Blaze\\Logs\\blaze.log'
$stdoutPath = Join-Path $ArtifactDirectory 'installed-blaze.stdout.log'
$stderrPath = Join-Path $ArtifactDirectory 'installed-blaze.stderr.log'
$dumpDirectory = Join-Path $ArtifactDirectory 'crash-dumps'
$failure = $null
$appProcess = $null
$results = [Collections.Generic.List[object]]::new()

function Add-Result {
    param([string]$Name, [bool]$Passed, [string]$Detail = '')
    $status = if ($Passed) { 'PASS' } else { 'FAIL' }
    $results.Add([ordered]@{ name = $Name; status = $status; detail = $Detail })
}
function Invoke-WinAppChecked {
    param([string[]]$Arguments)
    $global:LASTEXITCODE = $null
    $output = @(& winapp @Arguments)
    $code = $global:LASTEXITCODE
    if ($null -eq $code -or $code -ne 0) { throw "winapp $($Arguments -join ' ') failed (exit $code). $($output -join [Environment]::NewLine)" }
    $output
}
function Get-Elements {
    param($Nodes)
    foreach ($node in $Nodes) {
        if ($null -ne $node) { $node; if ($node.children) { Get-Elements $node.children } }
    }
}

try {
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) { throw "Installer does not exist: $InstallerPath" }
    if (Test-Path -LiteralPath $installDirectory) { throw "Pre-existing Blaze installation found at $installDirectory; refusing to overwrite it." }

    $installerProcess = Start-Process -FilePath $InstallerPath -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-') -Wait -PassThru
    if ($installerProcess.ExitCode -ne 0) { throw "Inno Setup exited with code $($installerProcess.ExitCode)." }
    Add-Result 'Silent per-user installer completed' $true

    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Installed executable is missing: $executable" }
    Add-Result 'Blaze.exe installed to configured per-user path' $true $executable

    foreach ($required in @('WebKitHost.dll','WebKit2.dll','webkit-sdk-manifest.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installDirectory $required) -PathType Leaf)) { throw "Installed WebKit payload is missing $required." }
    }
    Add-Result 'Native WebKit host and runtime installed' $true

    $resourceIndexes = @(Get-ChildItem -LiteralPath $installDirectory -Filter '*.pri' -File)
    if ($resourceIndexes.Count -eq 0) { throw 'Installed application is missing the WinUI .pri resource index.' }
    foreach ($resourceIndex in $resourceIndexes) { if ($resourceIndex.Length -le 0) { throw "Installed PRI is empty: $($resourceIndex.FullName)" } }
    Add-Result 'WinUI PRI resource index installed beside Blaze.exe' $true (($resourceIndexes.Name) -join ', ')

    if (Test-Path -LiteralPath $diagnosticLog -PathType Leaf) { Remove-Item -LiteralPath $diagnosticLog -Force }
    New-Item -ItemType Directory -Force -Path $dumpDirectory | Out-Null
    $dumpKey = 'HKCU:\\Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\Blaze.exe'
    New-Item -Path $dumpKey -Force | Out-Null
    New-ItemProperty -Path $dumpKey -Name DumpFolder -PropertyType ExpandString -Value $dumpDirectory -Force | Out-Null
    New-ItemProperty -Path $dumpKey -Name DumpType -PropertyType DWord -Value 2 -Force | Out-Null
    New-ItemProperty -Path $dumpKey -Name DumpCount -PropertyType DWord -Value 3 -Force | Out-Null

    $appProcess = Start-Process -FilePath $executable -WorkingDirectory $installDirectory -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    if (-not $appProcess -or $appProcess.Id -le 0) { throw 'Windows did not create the installed Blaze process.' }

    $mainWindow = $null
    $lastWindowOutput = ''
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        if (-not (Get-Process -Id $appProcess.Id -ErrorAction SilentlyContinue)) { break }
        try {
            $windowOutput = @(Invoke-WinAppChecked @('ui','list-windows','--json','-a',[string]$appProcess.Id))
            $lastWindowOutput = $windowOutput -join [Environment]::NewLine
            $windows = @($lastWindowOutput | ConvertFrom-Json)
            $mainWindow = $windows | Where-Object { $_.hwnd -and $_.hwnd -ne '0' -and $_.title -like 'Blaze*' } | Select-Object -First 1
            if ($mainWindow) { break }
        } catch { $lastWindowOutput = "$_" }
        Start-Sleep -Milliseconds 500
    }
    if (-not $mainWindow) {
        $process = Get-Process -Id $appProcess.Id -ErrorAction SilentlyContinue
        $state = if ($process) { "Process remains alive; MainWindowHandle=$($process.MainWindowHandle)." } else { 'Installed Blaze process exited before creating its window.' }
        $log = if (Test-Path -LiteralPath $diagnosticLog -PathType Leaf) { Get-Content -LiteralPath $diagnosticLog -Raw } else { '<no Blaze diagnostic log was produced>' }
        $stdout = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath -Raw } else { '' }
        $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath -Raw } else { '' }
        throw ("Installed app failed to create its main window. {0} Last window enumeration: {1}{2}Startup log:{2}{3}{2}stdout:{2}{4}{2}stderr:{2}{5}" -f $state,$lastWindowOutput,[Environment]::NewLine,$log,$stdout,$stderr)
    }

    $windowHandle = [string]$mainWindow.hwnd
    $inspectionOutput = Invoke-WinAppChecked @('ui','inspect','--interactive','--json','-w',$windowHandle)
    $inspection = ($inspectionOutput -join [Environment]::NewLine) | ConvertFrom-Json
    $elements = @(Get-Elements @($inspection.windows | ForEach-Object { $_.elements }))
    if (-not ($elements | Where-Object { $_.automationId -eq 'AddressBox' })) { throw 'Installed window did not expose the AddressBox WinUI control.' }
    Add-Result 'Installed WinUI main window and address control initialized' $true

    $screenshot = Join-Path $ArtifactDirectory 'installed-blaze-startup.png'
    Invoke-WinAppChecked @('ui','screenshot','--output',$screenshot,'-w',$windowHandle) | Out-Null
    if (-not (Test-Path -LiteralPath $screenshot -PathType Leaf) -or (Get-Item -LiteralPath $screenshot).Length -eq 0) { throw 'Installed application screenshot was not captured.' }
    Add-Result 'Installed application startup screenshot captured' $true $screenshot
} catch {
    $failure = "$_"
    Add-Result 'Installed application integration test' $false $failure
} finally {
    if ($appProcess -and (Get-Process -Id $appProcess.Id -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $appProcess.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $appProcess.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $diagnosticLog -PathType Leaf) {
        Copy-Item -LiteralPath $diagnosticLog -Destination (Join-Path $ArtifactDirectory 'blaze-diagnostic.log') -Force
    }
    $uninstaller = Join-Path $installDirectory 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller -PathType Leaf) {
        try {
            $uninstallerProcess = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -Wait -PassThru
            if ($uninstallerProcess.ExitCode -ne 0) { Add-Result 'Silent uninstall removed installed application' $false "Exit code: $($uninstallerProcess.ExitCode)" }
            elseif (Test-Path -LiteralPath $installDirectory) { Add-Result 'Silent uninstall removed installed application' $false "Install directory remains: $installDirectory" }
            else { Add-Result 'Silent uninstall removed installed application' $true }
        } catch { Add-Result 'Silent uninstall removed installed application' $false "$_" }
    } elseif (Test-Path -LiteralPath $installDirectory) {
        Add-Result 'Silent uninstall removed installed application' $false 'The installer did not create unins000.exe.'
    }
    Remove-Item -Path 'HKCU:\\Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\Blaze.exe' -Recurse -Force -ErrorAction SilentlyContinue
}

$failed = @($results | Where-Object { $_.status -eq 'FAIL' }).Count
$report = [ordered]@{ installer = $InstallerPath; installDirectory = $installDirectory; passed = $results.Count - $failed; failed = $failed; results = @($results.ToArray()) }
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'installer-test-results.json') -Encoding utf8
Write-Host "Installed-app integration test: $($report.passed) passed, $failed failed."
if ($failure) { throw $failure }
if ($failed -gt 0) { throw 'One or more installed-app integration checks failed.' }
