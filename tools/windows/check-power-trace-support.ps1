[CmdletBinding()]
param([string]$OutputDirectory = '.')

function Get-PowerTraceFailureStatus {
    param($ErrorRecord)

    if ($ErrorRecord.CategoryInfo.Category -in @('PermissionDenied', 'SecurityError') -or
        $ErrorRecord.Exception -is [UnauthorizedAccessException]) {
        return 'inacessivel'
    }
    if ($ErrorRecord.FullyQualifiedErrorId -like 'NoMatchingProvidersFound,*') {
        return 'ausente'
    }
    return 'nao_verificavel'
}

function Find-PowerTraceTool {
    param([ValidateSet('wpr.exe', 'wpa.exe', 'logman.exe')][string]$Name)

    $command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) { return $command.Path }

    $systemTool = Join-Path ([Environment]::SystemDirectory) $Name
    if (Test-Path -LiteralPath $systemTool -PathType Leaf) { return $systemTool }

    if ($Name -eq 'wpa.exe') {
        foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles) | Where-Object { $_ }) {
            $path = Join-Path $root 'Windows Kits\10\Windows Performance Toolkit\wpa.exe'
            if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
        }
    }
}

function Get-PowerTraceManifest {
    param([string]$Name)

    $result = [ordered]@{ name = $Name; registration = 'nao_verificavel'; schema = 'nao_verificavel'; events = @() }
    try {
        $provider = Get-WinEvent -ListProvider $Name -ErrorAction Stop
        if ($null -eq $provider) { return $result }
        $result.registration = 'encontrado'
    }
    catch {
        $result.registration = Get-PowerTraceFailureStatus $_
        return $result
    }

    # These names are search hints, not a contract for WPA or a schema allowlist for recording.
    $identity = @('DeviceInstanceId', 'InstanceId', 'DeviceId', 'PnpDeviceId', 'HardwareId',
        'DeviceName', 'DeviceObject', 'PhysicalDeviceObject', 'PDO', 'DeviceHandle', 'PoFxDevice')
    $state = @('DState', 'DeviceDState', 'DevicePowerState', 'PowerState', 'NewState',
        'OldState', 'TargetState', 'State')
    $request = @('Irp', 'IrpId', 'PowerIrp', 'IrpDuration', 'Duration', 'Status',
        'CompletionStatus', 'MinorFunction', 'PowerType', 'StateType')
    $knownFields = $identity + $state + $request

    try {
        $schemas = @($provider.Events | Where-Object { $_.Template })
        $result.events = @(foreach ($event in $schemas) {
            [xml]$template = $event.Template
            $fields = @($template.SelectNodes("//*[local-name()='data' or local-name()='struct']") |
                ForEach-Object { $_.GetAttribute('name') })
            $matches = @($knownFields | Where-Object { $_ -in $fields })
            if ($matches.Count -eq 0) { continue }

            [ordered]@{
                id = [int]$event.Id
                version = [int]$event.Version
                level = if ($null -ne $event.Level) { [int]$event.Level.Value }
                keywordMasks = @($event.Keywords | Where-Object { $null -ne $_ } |
                    ForEach-Object { '0x{0:X16}' -f [long]$_.Value })
                fields = $matches
                otherFieldCount = @($fields | Where-Object { $_ -notin $knownFields }).Count
            }
        })
        if ($schemas.Count -gt 0) { $result.schema = 'encontrado' }
    }
    catch {
        $result.schema = Get-PowerTraceFailureStatus $_
        $result.events = @()
    }
    return $result
}

function Get-PowerTraceSessionHints {
    param([string]$WprPath, [string]$LogmanPath)

    $result = [ordered]@{ wprRecording = 'nao_verificavel'; sharedLoggerHint = 'nao_verificavel' }
    if ($WprPath) {
        try {
            $lines = @(& $WprPath -status 2>&1)
            if ($LASTEXITCODE -eq 0 -and $lines -match '^WPR recording is in progress') {
                $result.wprRecording = 'encontrado'
            }
            elseif ($LASTEXITCODE -eq 0 -and $lines -match '^WPR is not recording') {
                $result.wprRecording = 'ausente'
            }
        }
        catch { $result.wprRecording = Get-PowerTraceFailureStatus $_ }
    }

    if ($LogmanPath) {
        try {
            $lines = @(& $LogmanPath query -ets 2>&1)
            if ($LASTEXITCODE -eq 0 -and $lines -match '^\s*(NT Kernel Logger|WPR_initiated_\S*)(?:\s{2,}|\s*$)') {
                $result.sharedLoggerHint = 'encontrado'
            }
        }
        catch { $result.sharedLoggerHint = Get-PowerTraceFailureStatus $_ }
    }
    return $result
}

function Get-PowerTraceSupportReport {
    $wpr = Find-PowerTraceTool 'wpr.exe'
    $wpa = Find-PowerTraceTool 'wpa.exe'
    $logman = Find-PowerTraceTool 'logman.exe'

    [ordered]@{
        schemaVersion = 1
        collectedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm')
        windowsBuild = [Environment]::OSVersion.Version.Build
        tools = [ordered]@{
            wpr = if ($wpr) { 'encontrado' } else { 'ausente' }
            wpa = if ($wpa) { 'encontrado' } else { 'ausente' }
            logman = if ($logman) { 'encontrado' } else { 'ausente' }
        }
        providers = @(
            Get-PowerTraceManifest 'Microsoft-Windows-Kernel-Power'
            Get-PowerTraceManifest 'Microsoft-Windows-PDC'
        )
        sessions = Get-PowerTraceSessionHints $wpr $logman
        targetCoverage = 'nao_verificavel'
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'Execute a pre-verificacao no Windows.'
    }
    $report = Get-PowerTraceSupportReport
    $directory = [IO.Directory]::CreateDirectory(
        $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory))
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff')
    $path = Join-Path $directory.FullName "goodix-power-trace-support_${stamp}_UTC.json"
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew)
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($report | ConvertTo-Json -Depth 8))
        $stream.Write($bytes, 0, $bytes.Length)
    }
    finally { $stream.Dispose() }
    $path
}
