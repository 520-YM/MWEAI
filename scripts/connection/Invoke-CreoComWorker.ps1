$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$request = $env:BWTL_CREO_COM_REQUEST | ConvertFrom-Json
[Environment]::SetEnvironmentVariable('BWTL_CREO_COM_REQUEST', $null)
$connection = $null
$stage = 'COM factory initialization'

function Get-CreoFactory([guid]$ClassId) {
    $type = [Type]::GetTypeFromCLSID($ClassId)
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        try { return [Activator]::CreateInstance($type) }
        catch {
            if ($_.Exception.GetBaseException().HResult -ne -2147221164 -or $clock.ElapsedMilliseconds -gt 1800) { throw }
            Start-Sleep -Milliseconds 100
        }
    } while ($true)
}

function Get-CreoComState($Session) {
    $window = $Session.CurrentWindow
    $windowId = if ($null -eq $window) { -1 } else { [int]$window.GetId() }
    $model = $null
    if ($windowId -ge 0) { $model = $window.Model }
    $workDir = [IO.Path]::GetFullPath([string]$Session.GetCurrentDirectory()).TrimEnd('\')
    [pscustomobject]@{ Model = if ($null -ne $model) { [string]$model.FileName } else { '' }; Window = $windowId; WorkDir = $workDir }
}

try {
    $factory = Get-CreoFactory '456e0110-2031-3907-afe5-9201c97a915e'
    $idFactory = Get-CreoFactory '7ff18882-452c-3a7d-b623-39624db05241'
    $stage = 'ConnectById'
    $connectionId = $idFactory.Create([string]$request.ConnectionId)
    $connection = $factory.ConnectById($connectionId, $null, [int]$request.TimeoutSeconds)
    if ($null -eq $connection -or !$connection.IsRunning() -or
        [string]$connection.GetConnectionId().ExternalRep -cne [string]$request.ConnectionId) {
        throw 'The live COM connection identity does not match this Creo process.'
    }
    $session = $connection.Session
    $stage = 'COM session readback'
    $result = Get-CreoComState $session
    if ($result.WorkDir -ine $request.ExpectedWorkDir -or $result.Model -ine $request.ExpectedModel -or
        ($null -ne $request.ExpectedWindow -and $result.Window -ne $request.ExpectedWindow)) {
        throw 'The COM window, model or working directory no longer matches the native handoff.'
    }

    if ($request.Operation -eq 'Activate') {
        $stage = 'COM target verification'
        $targetModel = $session.GetModelFromFileName([string]$request.TargetModel)
        if ($null -eq $targetModel -or [string]$targetModel.FileName -ine $request.TargetModel) {
            throw 'The attested target is no longer loaded.'
        }
        if ($request.TargetPath -and [IO.Path]::GetFullPath([string]$targetModel.Origin) -ine $request.TargetPath) {
            throw 'The loaded Creo file origin does not match the exact E drive target.'
        }

        $stage = 'COM window activation'
        $targetWindow = $session.GetModelWindow($targetModel)
        if ($null -eq $targetWindow) {
            $targetWindow = $session.CreateModelWindow($targetModel)
            $targetModel.Display()
        }
        if ($null -eq $targetWindow -or [string]$targetWindow.Model.FileName -ine $request.TargetModel) {
            throw 'The Creo window is not attached to the attested target model.'
        }
        $targetWindowId = [int]$targetWindow.GetId()
        [void]$targetWindow.Activate()
        $clock = [Diagnostics.Stopwatch]::StartNew()
        do {
            $result = Get-CreoComState $session
            if ($result.Window -eq $targetWindowId -and $result.Model -ieq $request.TargetModel) { break }
            Start-Sleep -Milliseconds 75
        } while ($clock.ElapsedMilliseconds -lt 800)
        if ($result.Window -ne $targetWindowId -or $result.Model -ine $request.TargetModel -or
            $result.WorkDir -ine $request.ExpectedWorkDir) { throw 'COM activation readback did not identify the attested target window.' }
    }
    $result | ConvertTo-Json -Compress
    exit 0
}
catch {
    $detail = $_.Exception.GetBaseException()
    $message = if ($request.ConnectionId) { $detail.Message.Replace([string]$request.ConnectionId, '[connection ID]') } else { $detail.Message }
    [Console]::Error.WriteLine(('{0} (0x{1:X8}): {2}' -f $stage,$detail.HResult,$message))
    exit 1
}
finally {
    if ($null -ne $connection) {
        try { [void]$connection.Disconnect([int]$request.TimeoutSeconds) }
        catch { [Console]::Error.WriteLine('Disconnect did not return cleanly.') }
    }
}
