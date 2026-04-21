[CmdletBinding()]
param(
    [string]$Python = "python",
    [string]$TcpHost = "0.0.0.0",
    [int]$TcpPort = 23333,
    [string]$HttpHost = "127.0.0.1",
    [int]$HttpPort = 8080,
    [switch]$WithMock,
    [switch]$NoBrowser,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Port {
    param(
        [int]$Port,
        [string]$Name
    )

    if ($Port -lt 1 -or $Port -gt 65535) {
        throw "$Name must be within 1..65535."
    }
}

function Resolve-PythonExecutable {
    param([string]$Name)

    $command = Get-Command $Name -ErrorAction Stop
    if ($command.Path) {
        return $command.Path
    }
    if ($command.Source) {
        return $command.Source
    }
    return $command.Name
}

function Quote-Single {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Start-PythonWindow {
    param(
        [string]$Title,
        [string]$WorkingDirectory,
        [string]$PythonExecutable,
        [string]$ScriptPath,
        [string[]]$Arguments
    )

    $argumentListLiteral = if ($Arguments.Count -gt 0) {
        "@(" + (($Arguments | ForEach-Object { Quote-Single $_ }) -join ", ") + ")"
    }
    else {
        "@()"
    }

    $command = @(
        '$Host.UI.RawUI.WindowTitle = ' + (Quote-Single $Title)
        'Set-Location -LiteralPath ' + (Quote-Single $WorkingDirectory)
        '& ' + (Quote-Single $PythonExecutable) + ' ' + (Quote-Single $ScriptPath) + ' ' + $argumentListLiteral
    ) -join "; "

    if ($DryRun) {
        Write-Host "[dry-run] powershell.exe -NoExit -NoLogo -Command $command"
        return
    }

    Start-Process -FilePath "powershell.exe" -WorkingDirectory $WorkingDirectory -ArgumentList @(
        "-NoExit",
        "-NoLogo",
        "-Command",
        $command
    ) | Out-Null
}

Assert-Port -Port $TcpPort -Name "TcpPort"
Assert-Port -Port $HttpPort -Name "HttpPort"

$pcHostDir = $PSScriptRoot
$repoDir = Split-Path -Parent $pcHostDir
$pythonExecutable = Resolve-PythonExecutable -Name $Python
$hostScript = Join-Path $pcHostDir "run_host.py"
$mockScript = Join-Path $pcHostDir "run_mock_gateway.py"

if (-not (Test-Path -LiteralPath $hostScript)) {
    throw "Cannot find host entry: $hostScript"
}

if ($WithMock -and -not (Test-Path -LiteralPath $mockScript)) {
    throw "Cannot find mock gateway entry: $mockScript"
}

$hostArgs = @(
    "--tcp-host", $TcpHost,
    "--tcp-port", $TcpPort.ToString(),
    "--http-host", $HttpHost,
    "--http-port", $HttpPort.ToString()
)

Start-PythonWindow `
    -Title "ZNJJ Host" `
    -WorkingDirectory $repoDir `
    -PythonExecutable $pythonExecutable `
    -ScriptPath $hostScript `
    -Arguments $hostArgs

Write-Host "Started host window."
Write-Host ("TCP listener target: {0}:{1}" -f $TcpHost, $TcpPort)
Write-Host ("Dashboard URL: http://{0}:{1}/" -f $HttpHost, $HttpPort)

if ($WithMock) {
    Start-Sleep -Milliseconds 900

    $mockArgs = @(
        "--host", "127.0.0.1",
        "--port", $TcpPort.ToString()
    )

    Start-PythonWindow `
        -Title "ZNJJ Mock Gateway" `
        -WorkingDirectory $repoDir `
        -PythonExecutable $pythonExecutable `
        -ScriptPath $mockScript `
        -Arguments $mockArgs

    Write-Host "Started mock gateway window."
}

if (-not $NoBrowser) {
    $browserHost = if ($HttpHost -eq "0.0.0.0") { "127.0.0.1" } else { $HttpHost }
    $dashboardUrl = "http://{0}:{1}/" -f $browserHost, $HttpPort

    if ($DryRun) {
        Write-Host "[dry-run] open $dashboardUrl"
    }
    else {
        Start-Sleep -Milliseconds 1200
        Start-Process $dashboardUrl | Out-Null
    }
}

Write-Host ""
Write-Host "Close the spawned PowerShell windows to stop the services."
