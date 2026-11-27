$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot '../../tools/windows/check-power-trace-support.ps1'
$tokens = $null
$parseErrors = $null
$null = [System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'PowerShell parse failed' }
. $scriptPath

function Assert-Equal ($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'" }
    $script:checks++
}

$script:checks = 0
$script:queried = @()
function Get-WinEvent {
    [CmdletBinding()]
    param([string]$ListProvider)

    $script:queried += $ListProvider
    if ($ListProvider -eq 'Microsoft-Windows-PDC') {
        Write-Error 'PRIVATE error details' -ErrorId 'NoMatchingProvidersFound'
        return
    }
    if ($ListProvider -ne 'Microsoft-Windows-Kernel-Power') { throw 'Unexpected provider' }
    switch ($script:mode) {
        'denied' { throw [UnauthorizedAccessException]::new('PRIVATE access path') }
        'empty' { return @{ Events = @() } }
        'invalid' { return @{ Events = @(@{ Template = '<invalid' }) } }
    }
    return @{
        Events = @(
            @{
                Id = 42; Version = 2; Level = @{ Value = 4 }
                Keywords = @(@{ Value = [long]::MinValue })
                Template = '<template><data name="DeviceInstanceId"/><data name="DState"/><data name="PRIVATE_FIELD"/></template>'
            }
            @{ Id = 43; Template = '<template><data name="PRIVATE_ONLY"/></template>' }
        )
    }
}

function Find-PowerTraceTool {
    param([string]$Name)
    switch ($Name) {
        'wpr.exe' { 'Invoke-MockWpr' }
        'logman.exe' { 'Invoke-MockLogman' }
    }
}
function Invoke-MockWpr {
    if ($args.Count -ne 1 -or $args[0] -cne '-status') { throw 'Unexpected WPR command' }
    $global:LASTEXITCODE = $script:wprExit
    $script:wprText
}
function Invoke-MockLogman {
    if (($args -join ' ') -cne 'query -ets') { throw 'Unexpected logman command' }
    $global:LASTEXITCODE = $script:logmanExit
    $script:logmanText
}

$script:wprExit = 0
$script:logmanExit = 0
$script:wprText = 'WPR recording is in progress...'
$script:logmanText = 'WPR_initiated_PRIVATE   Trace'
$r = Get-PowerTraceSupportReport
Assert-Equal ($script:queried -join ',') 'Microsoft-Windows-Kernel-Power,Microsoft-Windows-PDC'
Assert-Equal $r.tools.wpr 'encontrado'
Assert-Equal $r.tools.wpa 'ausente'
Assert-Equal $r.providers[0].registration 'encontrado'
Assert-Equal $r.providers[0].schema 'encontrado'
Assert-Equal $r.providers[0].events.Count 1
Assert-Equal $r.providers[0].events[0].id 42
Assert-Equal $r.providers[0].events[0].version 2
Assert-Equal $r.providers[0].events[0].level 4
Assert-Equal ($r.providers[0].events[0].fields -join ',') 'DeviceInstanceId,DState'
Assert-Equal $r.providers[0].events[0].keywordMasks[0] '0x8000000000000000'
Assert-Equal $r.providers[0].events[0].otherFieldCount 1
Assert-Equal $r.providers[1].registration 'ausente'
Assert-Equal $r.targetCoverage 'nao_verificavel'
Assert-Equal $r.sessions.wprRecording 'encontrado'
Assert-Equal $r.sessions.sharedLoggerHint 'encontrado'
Assert-Equal (($r | ConvertTo-Json -Depth 8) -match 'PRIVATE|Invoke-Mock') $false

$script:mode = 'denied'
Assert-Equal (Get-PowerTraceManifest 'Microsoft-Windows-Kernel-Power').registration 'inacessivel'
$script:mode = 'empty'
Assert-Equal (Get-PowerTraceManifest 'Microsoft-Windows-Kernel-Power').schema 'nao_verificavel'
$script:mode = 'invalid'
Assert-Equal (Get-PowerTraceManifest 'Microsoft-Windows-Kernel-Power').schema 'nao_verificavel'
$script:wprText = 'WPR is not recording'
Assert-Equal (Get-PowerTraceSessionHints 'Invoke-MockWpr' '').wprRecording 'ausente'
$script:wprText = 'Resposta localizada'
Assert-Equal (Get-PowerTraceSessionHints 'Invoke-MockWpr' '').wprRecording 'nao_verificavel'
$script:logmanExit = 5
Assert-Equal (Get-PowerTraceSessionHints '' 'Invoke-MockLogman').sharedLoggerHint 'nao_verificavel'
Assert-Equal (Get-PowerTraceSessionHints '' '').sharedLoggerHint 'nao_verificavel'
"Passed $script:checks checks"
