#Requires -Version 5.1
$ErrorActionPreference = 'Stop'
$collector = Join-Path $PSScriptRoot '../../tools/windows/collect-controller-config.ps1'
foreach ($file in $collector, $PSCommandPath) {
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($file, [ref]$null, [ref]$parseErrors)
    if ($parseErrors) { throw ($parseErrors | Out-String) }
}
. $collector

$script:checks = 0
function Assert-Equal {
    param($Actual, $Expected, [string]$Label)
    if ($Actual -cne $Expected) { throw "Failed: $Label (expected $Expected, got $Actual)" }
    $script:checks++
}

$script:intel = 'PCI\VEN_8086&DEV_34AB&SUBSYS_03651854&REV_30\CONTROLLER_PRIVATE'
$script:sensor = 'ACPI\GXFP5A8B\SENSOR_PRIVATE'
$script:scenario = 'primary'

function Get-CimInstance {
    param($ClassName, $Filter, $Property, $ErrorAction)
    if ($ClassName -eq 'Win32_OperatingSystem') { return [pscustomobject]@{ BuildNumber = '26300' } }
    if ($Filter -ne "Name='iaLPSS2_SPI'") { throw 'unscoped service query' }
    [pscustomobject]@{
        State = 'Running'
        PathName = '\SystemRoot\System32\DriverStore\FileRepository\ialpss2_spi_icl.inf_amd64_a1\iaLPSS2_SPI.sys'
    }
}

function Get-PnpDevice {
    param([switch]$PresentOnly, $ErrorAction)
    if (-not $PresentOnly) { throw 'present-only filter missing' }
    if ($script:scenario -eq 'denied') { throw [UnauthorizedAccessException]::new('PRIVATE_USER') }
    [pscustomobject]@{ InstanceId = $script:intel; FriendlyName = 'PRIVATE_USER' }
    [pscustomobject]@{ InstanceId = $script:sensor; FriendlyName = 'PRIVATE_USER' }
    [pscustomobject]@{ InstanceId = 'PCI\VEN_8086&DEV_34ABB\UNRELATED_PRIVATE' }
    [pscustomobject]@{ InstanceId = 'ACPI\GXFP5A8B2\UNRELATED_PRIVATE' }
    if ($script:scenario -eq 'ambiguous') { [pscustomobject]@{ InstanceId = 'ACPI\GXFP5A8B\OTHER_PRIVATE' } }
}

function Get-PnpDeviceProperty {
    param($InstanceId, $KeyName, $ErrorAction)
    if ($InstanceId -notin $script:intel, $script:sensor) { throw 'unrelated PnP device queried' }
    switch ($KeyName) {
        'DEVPKEY_Device_Service' {
            if ($InstanceId -eq $script:intel) { return [pscustomobject]@{ Data = 'iaLPSS2_SPI' } }
            return [pscustomobject]@{ Data = 'WUDFRd' }
        }
        'DEVPKEY_Device_DriverVersion' { return [pscustomobject]@{ Data = '30.100.1932.6' } }
        'DEVPKEY_Device_DriverInfPath' { return [pscustomobject]@{ Data = 'oem22.inf' } }
        'DEVPKEY_Device_DriverInfSection' { return [pscustomobject]@{ Data = 'iaLPSS2_SPI_Device.NT' } }
        'DEVPKEY_Device_ProblemCode' {
            if ($InstanceId -eq $script:intel) { return [pscustomobject]@{ Type = 'Empty'; Data = $null } }
            return [pscustomobject]@{ Data = 'PRIVATE_USER|private data' }
        }
        'DEVPKEY_Device_PowerData' {
            if ($InstanceId -eq $script:sensor) { throw [UnauthorizedAccessException]::new('PRIVATE_USER') }
            $bytes = [byte[]]::new(56)
            [BitConverter]::GetBytes([uint32]56).CopyTo($bytes, 0)
            [BitConverter]::GetBytes([uint32]4).CopyTo($bytes, 4)
            [BitConverter]::GetBytes([uint32]123).CopyTo($bytes, 20)
            return [pscustomobject]@{ Data = $bytes }
        }
        default { throw 'unknown key' }
    }
}

function Get-FileHash {
    param($LiteralPath, $Algorithm, $ErrorAction)
    if ((Split-Path -Leaf $LiteralPath) -ne 'iaLPSS2_SPI.sys' -or $Algorithm -ne 'SHA256') {
        throw 'unexpected file request'
    }
    [pscustomobject]@{ Hash = 'efd2eea34f64d9ee8aaa1d4ac2885686504011547b1de3350e95d2884f561712' }
}

$script:mockRegistry = [pscustomobject]@{
    Values = @{
        SourceClockFrequency_1 = 120000000; DynamicClockGating_1 = 0; CsSetupTime_1 = 'PRIVATE_USER'
        DmaThreshold_2 = -1; DmaInit_3 = 1; DmaInit_4294967296 = 1
        CsHoldTime = 4; 'CsHoldTime_../PRIVATE_USER' = 5; UnrelatedSecret = 'PRIVATE_USER'
    }
    Reads = [System.Collections.Generic.List[string]]::new()
}
$script:mockRegistry | Add-Member ScriptMethod GetValueNames { @($this.Values.Keys) }
$script:mockRegistry | Add-Member ScriptMethod GetValue {
    param($Name, $Default, $Options)
    $this.Reads.Add($Name)
    if ($Name -eq 'DmaInit_3') { throw [UnauthorizedAccessException]::new('PRIVATE_USER') }
    $this.Values[$Name]
}
$script:mockRegistry | Add-Member ScriptMethod GetValueKind {
    param($Name)
    if ($this.Values[$Name] -is [int]) { 'DWord' } else { 'String' }
}
function Get-IntelRegistryRows {
    param($InstanceId, $Service)
    if ($InstanceId -ne $script:intel -or $Service -ne 'iaLPSS2_SPI') { throw 'wrong registry target' }
    Get-IntelParameterRows $script:mockRegistry (Get-IntelDefaults)
}

function Get-WinEvent {
    param($ListLog, [switch]$Force, $LogName, $FilterXPath, $MaxEvents, $ErrorAction)
    if ($ListLog) {
        if ($ListLog -eq 'Intel-iaLPSS2-SPI/Performance') {
            throw [System.Management.Automation.ItemNotFoundException]::new('PRIVATE_USER')
        }
        return [pscustomobject]@{ IsEnabled = $ListLog -ne 'Intel-iaLPSS2-SPI/Debug' }
    }
    if ($LogName -ne 'Microsoft-Windows-Kernel-PnP/Configuration' -or $MaxEvents -ne 20) {
        throw 'event query outside scope'
    }
    if ($FilterXPath.Contains($script:sensor)) { throw [UnauthorizedAccessException]::new('PRIVATE_USER') }
    if (-not $FilterXPath.Contains($script:intel) -or $FilterXPath -notlike '*2592000000*') { throw 'missing event filters' }
    $event = [pscustomobject]@{ Id = 410; TimeCreated = [datetime]'2026-11-27T12:00:00Z' }
    $event | Add-Member ScriptProperty Message { throw 'payload read attempted' }
    $event | Add-Member ScriptMethod ToXml { throw 'payload read attempted' }
    $event | Add-Member ScriptMethod Dispose {}
    $event
}

$oldSystemRoot = $env:SystemRoot
$tempDir = Join-Path ([IO.Path]::GetTempPath()) ('goodix-config-test-' + [guid]::NewGuid())
try {
    $env:SystemRoot = [IO.Path]::GetTempPath().TrimEnd('/', '\')
    [void][IO.Directory]::CreateDirectory($tempDir)
    $rows = @(Get-ControllerConfigRows 30)
    function Find-Row { param($Section, $Field) $rows | Where-Object { $_.section -eq $Section -and $_.field -eq $Field } }
    $parameterSection = 'Intel Parameters (candidatos por indice)'
    Assert-Equal (Find-Row 'Plataforma' 'Intel PCI (modelo)').value 'VEN_8086&DEV_34AB&SUBSYS_03651854&REV_30' 'PCI model only'
    Assert-Equal (Find-Row 'Plataforma' 'Goodix ACPI (HID)').value 'GXFP5A8B' 'ACPI HID only'
    Assert-Equal (Find-Row 'Controlador PnP' 'ProblemCode').status 'ausente' 'missing PnP value'
    Assert-Equal (Find-Row 'Sensor PnP' 'ProblemCode').status 'inacessivel' 'untrusted PnP value'
    Assert-Equal (Find-Row 'Controlador PnP' 'MostRecentPowerState').value 4 'power state'
    Assert-Equal (Find-Row 'Controlador PnP' 'D3Latency_us').value 123 'power latency'
    Assert-Equal (Find-Row 'Sensor PnP' 'PowerData').status 'inacessivel' 'unauthorized power'
    Assert-Equal (Find-Row $parameterSection 'SourceClockFrequency_1').value 120000000 'registry scalar'
    Assert-Equal (Find-Row $parameterSection 'DynamicClockGating_1').value 0 'preserve zero'
    Assert-Equal (Find-Row $parameterSection 'CsSetupTime_1').status 'inacessivel' 'type mismatch'
    Assert-Equal (Find-Row $parameterSection 'CsHoldTime').status 'ausente' 'no qualified value'
    Assert-Equal (Find-Row $parameterSection 'DmaThreshold_2').value ([uint32]::MaxValue) 'unsigned DWORD'
    Assert-Equal (Find-Row $parameterSection 'DmaInit_3').status 'inacessivel' 'failed per-value read'
    Assert-Equal (@($script:mockRegistry.Reads | Where-Object { $_ -notin @('SourceClockFrequency_1','DynamicClockGating_1','CsSetupTime_1','DmaThreshold_2','DmaInit_3') }).Count) 0 'no unrelated registry reads'
    Assert-Equal (Find-Row 'Pacote Intel' 'Servico instalado').value 'Running' 'associated service'
    Assert-Equal (Find-Row 'Defaults iniciais do SYS 30.100.1932.6 (referencia estatica)' 'CsHoldTime').value 0 'static default'
    Assert-Equal (Get-DriverBinaryRow '\\OTHER_HOST\PRIVATE_USER\iaLPSS2_SPI.sys').status 'inacessivel' 'reject remote path'
    Assert-Equal (Find-Row 'Canais de diagnostico' 'Intel-iaLPSS2-SPI/Debug').value $false 'disabled channel'
    Assert-Equal (Find-Row 'Canais de diagnostico' 'Intel-iaLPSS2-SPI/Performance').status 'ausente' 'missing channel'
    Assert-Equal (Find-Row 'Eventos PnP / Intel' 'evento').value 'ID 410, 2026-11-27 12:00 UTC' 'event metadata'
    Assert-Equal (Find-Row 'Eventos PnP / Goodix' 'ocorrencias').status 'inacessivel' 'event query failure'
    Assert-Equal (Select-TargetDevice @([pscustomobject]@{InstanceId='ACPI\GXFP5A8B2\OTHER'}) '\AACPI\\GXFP5A8B\\[0-9A-Z]+\z').Status 'ausente' 'near-match rejected'

    $report = @{ schemaVersion = 1; collectedUtc = '2026-11-27 12:00'; eventDays = 30; rows = $rows }
    $paths = @(Write-ControllerConfigReport $report $tempDir)
    Assert-Equal $paths.Count 2 'two reports'
    $markdown = [IO.File]::ReadAllText($paths[0])
    $json = [IO.File]::ReadAllText($paths[1])
    Assert-Equal ($markdown -match '^# Goodix' -and $markdown.Contains('CS 0 ativo baixo')) $true 'markdown content'
    Assert-Equal ($json | ConvertFrom-Json).schemaVersion 1 'JSON schema'
    Assert-Equal (($markdown + $json) -match '_PRIVATE|PRIVATE_USER|FileRepository|UnrelatedSecret') $false 'sanitized output'

    $script:scenario = 'ambiguous'
    $rows = @(Get-ControllerConfigRows 30)
    Assert-Equal (Find-Row 'Plataforma' 'Goodix ACPI (HID)').status 'inacessivel' 'ambiguous sensor'
    Assert-Equal (Find-Row 'Sensor PnP' 'propriedades PnP').status 'nao_aplicavel' 'ambiguous device skipped'

    $script:scenario = 'denied'
    $rows = @(Get-ControllerConfigRows 30)
    Assert-Equal (Find-Row 'Plataforma' 'Intel PCI (modelo)').status 'inacessivel' 'enumeration failure'
    Assert-Equal (Find-Row 'Eventos PnP / Intel' 'ocorrencias').status 'nao_aplicavel' 'events skipped without exact ID'
    "Passed $script:checks synthetic assertions."
}
finally {
    $env:SystemRoot = $oldSystemRoot
    if ([IO.Directory]::Exists($tempDir)) { [IO.Directory]::Delete($tempDir, $true) }
}
