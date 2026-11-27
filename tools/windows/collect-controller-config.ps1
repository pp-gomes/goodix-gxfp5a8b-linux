#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$OutputDirectory = (Get-Location).Path,
    [ValidateRange(1, 90)][int]$EventDays = 30
)

function New-ConfigRow {
    param($Section, $Field, $Status, $Value = $null)
    [pscustomobject]@{ section = $Section; field = $Field; status = $Status; value = $Value }
}

function Select-TargetDevice {
    param($Devices, [string]$Pattern)
    $matchesFound = @($Devices | Where-Object { $_.InstanceId -match $Pattern })
    if ($matchesFound.Count -eq 1) {
        return [pscustomobject]@{ Status = 'encontrado'; Device = $matchesFound[0] }
    }
    $status = 'ausente'
    if ($matchesFound.Count -gt 1) { $status = 'inacessivel'; $reason = 'multiplas instancias presentes' }
    else { $reason = $null }
    [pscustomobject]@{ Status = $status; Device = $null; Reason = $reason }
}

function Get-QueryFailureStatus {
    param($ErrorRecord, [string]$MissingId)
    if (($MissingId -and ($ErrorRecord.FullyQualifiedErrorId -split ',')[0] -eq $MissingId) -or
        $ErrorRecord.Exception -is [System.Management.Automation.ItemNotFoundException] -or
        $ErrorRecord.CategoryInfo.Category -eq 'ObjectNotFound' -or
        ($ErrorRecord.Exception.HResult -in -2147023728, -2147024894)) {
        return 'ausente'
    }
    'inacessivel'
}

function Get-SafePnpField {
    param($Section, $InstanceId, $Key, $Pattern)
    try {
        $property = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName "DEVPKEY_Device_$Key" -ErrorAction Stop
        if ($null -eq $property.Data -or $property.Type -eq 'Empty') {
            return New-ConfigRow $Section $Key 'ausente'
        }
        $value = $property.Data
        if ($value -is [array] -or [string]$value -notmatch $Pattern) {
            return New-ConfigRow $Section $Key 'inacessivel' 'tipo ou formato fora da lista permitida'
        }
        New-ConfigRow $Section $Key 'encontrado' $value
    }
    catch { New-ConfigRow $Section $Key (Get-QueryFailureStatus $_) }
}

function Read-PnpPowerData {
    param($Section, $InstanceId)
    try {
        $property = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName 'DEVPKEY_Device_PowerData' -ErrorAction Stop
        if ($null -eq $property.Data -or $property.Type -eq 'Empty') {
            return New-ConfigRow $Section 'PowerData' 'ausente'
        }
        $bytes = [byte[]]$property.Data
        if ($bytes.Count -lt 56 -or [BitConverter]::ToUInt32($bytes, 0) -ne 56) {
            return New-ConfigRow $Section 'PowerData' 'inacessivel' 'estrutura CM_POWER_DATA desconhecida'
        }
        $fields = [ordered]@{
            MostRecentPowerState = 4; CapabilitiesMask = 8; D1Latency_us = 12
            D2Latency_us = 16; D3Latency_us = 20; S0DeviceState = 28
            S1DeviceState = 32; S2DeviceState = 36; S3DeviceState = 40
            S4DeviceState = 44; S5DeviceState = 48; DeepestSystemWake = 52
        }
        foreach ($field in $fields.Keys) {
            New-ConfigRow $Section $field 'encontrado' ([BitConverter]::ToUInt32($bytes, $fields[$field]))
        }
    }
    catch { New-ConfigRow $Section 'PowerData' (Get-QueryFailureStatus $_) }
}

function Get-DeviceConfigRows {
    param($Section, $Device, [string]$SelectionStatus)
    if ($null -eq $Device) {
        return New-ConfigRow $Section 'propriedades PnP' 'nao_aplicavel' $SelectionStatus
    }
    $properties = [ordered]@{
        DriverVersion = '\A[0-9]{1,5}(\.[0-9]{1,5}){1,3}\z'
        DriverInfPath = '\Aoem[0-9]{1,4}\.inf\z'
        DriverInfSection = '\AiaLPSS2_SPI_Device(?:\.NT)?\z'
        Service = '\A(?:iaLPSS2_SPI(?:_ICL)?|WUDFRd|gfspi)\z'
        ProblemCode = '\A[0-9]{1,10}\z'
    }
    foreach ($key in $properties.Keys) {
        if ($Section -eq 'Sensor PnP' -and $key -eq 'DriverInfSection') { continue }
        Get-SafePnpField $Section $Device.InstanceId $key $properties[$key]
    }
    Read-PnpPowerData $Section $Device.InstanceId
}

function Read-AllowedDword {
    param($Key, $Section, $Name)
    try {
        if ($null -eq $Key) { return New-ConfigRow $Section $Name 'ausente' }
        $value = $Key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($null -eq $value) { return New-ConfigRow $Section $Name 'ausente' }
        if ($Key.GetValueKind($Name) -ne 'DWord') {
            return New-ConfigRow $Section $Name 'inacessivel' 'tipo diferente de REG_DWORD'
        }
        New-ConfigRow $Section $Name 'encontrado' ([BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$value), 0))
    }
    catch { New-ConfigRow $Section $Name (Get-QueryFailureStatus $_) }
}

function Get-IntelParameterRows {
    param($Key, $Defaults)
    # Service-wide key. The suffix is Intel's decimal controller index.
    # Read only values matching the names and the unsigned format used by this build.
    $section = 'Intel Parameters (candidatos por indice)'
    try {
        $pattern = '\A(' + (($Defaults.Keys | ForEach-Object { [regex]::Escape($_) }) -join '|') +
            ')_(0|[1-9][0-9]{0,9})\z'
        $names = @()
        if ($null -ne $Key) { $names = @($Key.GetValueNames()) }
        $allowed = @($names | Where-Object {
            if ($_ -match $pattern) {
                $index = [uint32]0
                [uint32]::TryParse($Matches[2], [ref]$index)
            }
        })
        foreach ($name in $Defaults.Keys) {
            $candidates = @($allowed | Where-Object { $_ -match ('\A' + $name + '_') } | Sort-Object)
            if ($candidates.Count -eq 0) { New-ConfigRow $section $name 'ausente' }
            foreach ($candidate in $candidates) { Read-AllowedDword $Key $section $candidate }
        }
    }
    catch { New-ConfigRow $section 'leitura dos indices' (Get-QueryFailureStatus $_) }
}

function Get-IntelDefaults {
    [ordered]@{
        SourceClockFrequency = 120000000; DividerMValue = 0; LoopbackMode = 0
        MosiStateOnRead = 0; DynamicClockGating = 1; MonitorOnIdleTimeout = 2000
        MonitorOffIdleTimeout = 200; DmaInit = 1; DmaThreshold = 32
        CsSetupTime = 0; CsHoldTime = 0
    }
}

function Get-DriverBinaryRow {
    param($PathName)
    $section = 'Pacote Intel'
    # Resolve only the expected basename in Windows driver directories.
    $pattern = '\A(?:(?:\\SystemRoot\\|%SystemRoot%\\)|' + [regex]::Escape($env:SystemRoot + '\') +
        ')(System32\\(?:drivers|DriverStore\\FileRepository\\ialpss2_spi_icl\.inf_amd64_[a-f0-9]+)\\ialpss2_spi\.sys)\z'
    if ([string]$PathName -notmatch $pattern) {
        return New-ConfigRow $section 'SHA256 do SYS no caminho do servico' 'inacessivel' 'caminho fora da lista permitida'
    }
    $relativePath = $Matches[1]
    try {
        $hash = (Get-FileHash -LiteralPath (Join-Path $env:SystemRoot $relativePath) -Algorithm SHA256 -ErrorAction Stop).Hash
        if ($hash -notmatch '\A[0-9a-f]{64}\z') {
            return New-ConfigRow $section 'SHA256 do SYS no caminho do servico' 'inacessivel'
        }
        New-ConfigRow $section 'SHA256 do SYS no caminho do servico' 'encontrado' $hash.ToLowerInvariant()
    }
    catch { New-ConfigRow $section 'SHA256 do SYS no caminho do servico' (Get-QueryFailureStatus $_) }
}

function Get-IntelRegistryRows {
    param($InstanceId, $Service)
    $base = $null
    $serviceKey = $null
    $parameters = $null
    $wdf = $null
    try {
        $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
        $servicePath = "SYSTEM\CurrentControlSet\Services\$Service"
        $serviceKey = $base.OpenSubKey($servicePath, $false)
        if ($null -eq $serviceKey) { New-ConfigRow 'Pacote Intel' 'Dependencia SpbCx' 'ausente' }
        else {
            $dependencies = $serviceKey.GetValue('DependOnService')
            if ($dependencies -is [string[]] -and $dependencies -contains 'SpbCx') {
                New-ConfigRow 'Pacote Intel' 'Dependencia SpbCx' 'encontrado' $true
            }
            elseif ($null -eq $dependencies -or $dependencies -is [string[]]) {
                New-ConfigRow 'Pacote Intel' 'Dependencia SpbCx' 'ausente'
            }
            else { New-ConfigRow 'Pacote Intel' 'Dependencia SpbCx' 'inacessivel' }
        }
    }
    catch { New-ConfigRow 'Pacote Intel' 'Dependencia SpbCx' (Get-QueryFailureStatus $_) }
    try {
        $parameters = $base.OpenSubKey("$servicePath\Parameters", $false)
        Get-IntelParameterRows $parameters (Get-IntelDefaults)
    }
    catch { New-ConfigRow 'Intel Parameters (candidatos por indice)' 'chave Parameters' (Get-QueryFailureStatus $_) }
    try {
        $wdf = $base.OpenSubKey("SYSTEM\CurrentControlSet\Enum\$InstanceId\Device Parameters\WDF", $false)
        Read-AllowedDword $wdf 'Politica WDF persistente' 'WdfDirectedPowerTransitionEnable'
    }
    catch { New-ConfigRow 'Politica WDF persistente' 'WdfDirectedPowerTransitionEnable' (Get-QueryFailureStatus $_) }
    finally {
        foreach ($key in @($wdf, $parameters, $serviceKey, $base)) {
            if ($null -ne $key) { $key.Dispose() }
        }
    }
}

function Get-ChannelRow {
    param([string]$Name)
    try {
        $channel = Get-WinEvent -ListLog $Name -Force -ErrorAction Stop
        New-ConfigRow 'Canais de diagnostico' $Name 'encontrado' ([bool]$channel.IsEnabled)
    }
    catch { New-ConfigRow 'Canais de diagnostico' $Name (Get-QueryFailureStatus $_ 'NoMatchingLogsFound') }
}

function Get-PnpEventRows {
    param($Role, $InstanceId, $Days)
    $section = "Eventos PnP / $Role"
    # InstanceId passed the strict hardware-ID pattern. Event payloads are filtered
    # in the event service. Only scalar system metadata reaches the report.
    $window = [long]$Days * 86400000
    $xpath = "*[System[Provider[@Name='Microsoft-Windows-Kernel-PnP'] and " +
        "(EventID=400 or EventID=410 or EventID=411) and TimeCreated[timediff(@SystemTime) <= $window]] " +
        "and EventData[Data[@Name='DeviceInstanceId']='$InstanceId']]"
    try {
        $events = @(Get-WinEvent -LogName 'Microsoft-Windows-Kernel-PnP/Configuration' -FilterXPath $xpath -MaxEvents 20 -ErrorAction Stop)
        if ($events.Count -eq 0) { return New-ConfigRow $section 'ocorrencias' 'ausente' }
        foreach ($event in $events) {
            try {
                if ($event.Id -in 400, 410, 411 -and $null -ne $event.TimeCreated) {
                    $eventTime = $event.TimeCreated.ToUniversalTime().ToString('yyyy-MM-dd HH:mm')
                    New-ConfigRow $section 'evento' 'encontrado' "ID $($event.Id), $eventTime UTC"
                }
            }
            finally { $event.Dispose() }
        }
    }
    catch { New-ConfigRow $section 'ocorrencias' (Get-QueryFailureStatus $_ 'NoMatchingEventsFound') }
}

function Get-ControllerConfigRows {
    param([int]$Days)
    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -Property BuildNumber -ErrorAction Stop
        if ([string]$os.BuildNumber -match '\A[0-9]{4,6}\z') {
            New-ConfigRow 'Plataforma' 'Windows build' 'encontrado' $os.BuildNumber
        }
        else { New-ConfigRow 'Plataforma' 'Windows build' 'inacessivel' }
    }
    catch { New-ConfigRow 'Plataforma' 'Windows build' (Get-QueryFailureStatus $_) }
    try {
        $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop)
        $controller = Select-TargetDevice $devices '\APCI\\VEN_8086&DEV_34AB(?:&SUBSYS_[0-9A-F]{8})?(?:&REV_[0-9A-F]{2})?\\[0-9A-Z&._-]+\z'
        $sensor = Select-TargetDevice $devices '\AACPI\\GXFP5A8B\\[0-9A-Z&._-]+\z'
    }
    catch {
        $status = Get-QueryFailureStatus $_
        $controller = @{ Status = $status; Device = $null }
        $sensor = $controller
    }
    $controllerId = $controller.Device.InstanceId
    $sensorId = $sensor.Device.InstanceId
    $pciModel = $null
    if ($controllerId) { $pciModel = $controllerId.Split('\')[1].ToUpperInvariant() }
    New-ConfigRow 'Plataforma' 'Intel PCI (modelo)' $controller.Status $(if ($pciModel) { $pciModel } else { $controller.Reason })
    New-ConfigRow 'Plataforma' 'Goodix ACPI (HID)' $sensor.Status $(if ($sensorId) { 'GXFP5A8B' } else { $sensor.Reason })
    $intelFields = @(Get-DeviceConfigRows 'Controlador PnP' $controller.Device $controller.Status)
    $intelFields
    Get-DeviceConfigRows 'Sensor PnP' $sensor.Device $sensor.Status

    $service = ($intelFields | Where-Object { $_.field -eq 'Service' -and $_.status -eq 'encontrado' }).value
    if ($service -in 'iaLPSS2_SPI', 'iaLPSS2_SPI_ICL') {
        $hashRow = $null
        try {
            $installed = Get-CimInstance Win32_SystemDriver -Filter "Name='$service'" -Property State,PathName -ErrorAction Stop
            if ($null -eq $installed) { New-ConfigRow 'Pacote Intel' 'Servico instalado' 'ausente' }
            else {
                $state = $installed.State
                if ($state -in 'Running', 'Stopped', 'Start Pending', 'Stop Pending', 'Paused', 'Pause Pending', 'Continue Pending') {
                    New-ConfigRow 'Pacote Intel' 'Servico instalado' 'encontrado' $state
                }
                else { New-ConfigRow 'Pacote Intel' 'Servico instalado' 'inacessivel' 'estado fora da lista permitida' }
                $hashRow = Get-DriverBinaryRow $installed.PathName
                $hashRow
            }
        }
        catch { New-ConfigRow 'Pacote Intel' 'Servico instalado' (Get-QueryFailureStatus $_) }
        Get-IntelRegistryRows $controllerId $service
        if ($hashRow.value -eq 'efd2eea34f64d9ee8aaa1d4ac2885686504011547b1de3350e95d2884f561712') {
            $defaults = Get-IntelDefaults
            foreach ($name in $defaults.Keys) {
                New-ConfigRow 'Defaults iniciais do SYS 30.100.1932.6 (referencia estatica)' $name 'encontrado' $defaults[$name]
            }
        }
        else { New-ConfigRow 'Pacote Intel' 'Defaults de referencia' 'nao_aplicavel' 'hash de referencia sem correspondencia confirmada' }
    }
    else { New-ConfigRow 'Pacote Intel' 'Parametros e arquivo do servico' 'nao_aplicavel' 'servico Intel sem correspondencia confirmada' }

    if ($sensorId -and $pciModel -eq 'VEN_8086&DEV_34AB&SUBSYS_03651854&REV_30') {
        New-ConfigRow 'Conexao SPI' 'perfil ACPI historico do modelo de referencia' 'encontrado' 'CS 0 ativo baixo, modo 0, 8 bits, 4 fios, teto de 10 MHz'
    }
    New-ConfigRow 'Conexao SPI' 'descritor SPB e associacao sensor-controlador desta inicializacao' 'nao_aplicavel' 'fora da coleta administrativa'
    New-ConfigRow 'Conexao SPI' 'clock, CS e energia durante transferencia' 'nao_aplicavel' 'requer evidencia contemporanea adicional'

    foreach ($name in 'System', 'Intel-iaLPSS2-SPI/Debug', 'Intel-iaLPSS2-SPI/Performance') {
        Get-ChannelRow $name
    }
    $pnpChannel = Get-ChannelRow 'Microsoft-Windows-Kernel-PnP/Configuration'
    $pnpChannel
    foreach ($target in @(@{ Role = 'Intel'; Id = $controllerId }, @{ Role = 'Goodix'; Id = $sensorId })) {
        if ($pnpChannel.status -eq 'encontrado' -and $pnpChannel.value -and $target.Id) {
            Get-PnpEventRows $target.Role $target.Id $Days
        }
        else { New-ConfigRow "Eventos PnP / $($target.Role)" 'ocorrencias' 'nao_aplicavel' 'canal ou instancia indisponivel' }
    }
}

function Write-ControllerConfigReport {
    param($Report, [string]$Directory)
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff')
    $stem = Join-Path $Directory "goodix-controller-config_${stamp}_UTC"
    $lines = @(
        '# Goodix GXFP5A8B - Configuracao do controlador Windows', '',
        "Coleta UTC $($Report.collectedUtc). Janela de eventos $($Report.eventDays) dias, ate 20 por dispositivo.", '',
        'Estados: encontrado, ausente, inacessivel e nao_aplicavel. Ausencia de um valor persistente nao determina o default aplicado.',
        'Intel Parameters e compartilhada pelo servico. Os sufixos sao indices internos do driver; candidatos nao provam vinculo com o PCI selecionado.',
        'Defaults iniciais sao referencia do arquivo, sujeitos a ajustes por variante e plataforma. O hash identifica o arquivo em disco.',
        'PowerData descreve capacidades e ultimo estado reportado pelo PnP. Para DeviceState, 0 indica indefinido e 1 a 4 indicam D0 a D3.',
        'O perfil ACPI historico e referencia documental. Eventos PnP registram instalacao/inicializacao e falhas de inicio.',
        'Os canais Intel sao consultados apenas por metadados. Nenhum payload de evento integra a saida.', '',
        '| Escopo | Campo | Estado | Valor |', '| --- | --- | --- | --- |'
    )
    $lines += foreach ($row in $Report.rows) {
        '| {0} | {1} | {2} | {3} |' -f $row.section, $row.field, $row.status, $row.value
    }
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllLines("$stem.md", [string[]]$lines, $encoding)
    [IO.File]::WriteAllText("$stem.json", ($Report | ConvertTo-Json -Depth 5), $encoding)
    "$stem.md"
    "$stem.json"
}

if ($MyInvocation.InvocationName -ne '.') {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'Execute a coleta no Windows. Os testes sinteticos podem rodar sem Windows.'
    }
    $directory = [IO.Directory]::CreateDirectory($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory))
    $report = [ordered]@{
        schemaVersion = 1
        collectedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm')
        eventDays = $EventDays
        rows = @(Get-ControllerConfigRows $EventDays)
    }
    Write-ControllerConfigReport $report $directory.FullName
}
