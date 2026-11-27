function New-BwtlPtcFactory([guid]$ClassId) {
    $type=[Type]::GetTypeFromCLSID($ClassId)
    $clock=[Diagnostics.Stopwatch]::StartNew()
    do{
        try{return [Activator]::CreateInstance($type)}
        catch{
            if($_.Exception.GetBaseException().HResult -ne -2147221164 -or $clock.ElapsedMilliseconds -gt 1800){throw}
            Start-Sleep -Milliseconds 100
        }
    }while($true)
}

function Connect-BwtlPtc([string]$ConnectionId) {
    if(!$ConnectionId -or $ConnectionId.Length -gt 1024 -or $ConnectionId -match '[\x00-\x1f\x7f]'){
        throw 'Invalid Creo VB API connection ID.'
    }
    $factory=New-BwtlPtcFactory '456e0110-2031-3907-afe5-9201c97a915e'
    $builder=New-BwtlPtcFactory '7ff18882-452c-3a7d-b623-39624db05241'
    $connection=$null
    try{
        $id=$builder.Create($ConnectionId)
        $connection=$factory.ConnectById($id,$null,5)
        if($null -eq $connection -or !$connection.IsRunning() -or
            [string]$connection.GetConnectionId().ExternalRep -cne $ConnectionId){
            throw 'The live COM connection identity does not match the BWTL native identity.'
        }
        return $connection
    }catch{
        if($null -ne $connection){try{[void]$connection.Disconnect(1)}catch{}}
        $message=$_.Exception.GetBaseException().Message.Replace($ConnectionId,'[connection ID]')
        throw ('PTC ConnectById reported: '+$message)
    }
}

function Get-BwtlPtcState($Session,[bool]$AllowEmptyModel) {
    $model=$null
    $window=$Session.CurrentWindow
    $windowId=if($null -eq $window){-1}else{[int]$window.GetId()}
    if($windowId -ge 0){
        try{$model=$window.Model}
        catch{
            if(!$AllowEmptyModel -or $_.Exception.GetBaseException().Message -notmatch 'XToolkitNotFound|XToolkitBadContext'){throw}
        }
    }
    $workDir=[IO.Path]::GetFullPath([string]$Session.GetCurrentDirectory()).TrimEnd('\')
    [pscustomobject]@{Model=if($null -ne $model){[string]$model.FileName}else{''};Window=$windowId;WorkDir=$workDir;Mode='Resident'}
}

function Assert-BwtlPtcState($Actual,[string]$WorkDir,[string]$Model,$Window,[string]$Stage='initial connection') {
    if($Actual.WorkDir -ine $WorkDir.TrimEnd('\') -or $Actual.Model -ine $Model -or
        ($null -ne $Window -and $Actual.Window -ne $Window -and !(!$Model -and $Actual.Window -eq -1))){
        throw ('PTC readback at {0} differs from BWTL. Expected model [{1}], window [{2}], directory [{3}]. Read model [{4}], window [{5}], directory [{6}].' -f
            $Stage,$Model,$Window,$WorkDir,$Actual.Model,$Actual.Window,$Actual.WorkDir)
    }
}

function Invoke-BwtlPtcActivation($Endpoint,$Session,$Request) {
    if($Request.TargetModel -notmatch '(?i)^[^\\/:\x00-\x20]+\.(prt|asm)$' -or
        $Request.Lease -cnotmatch ('^{0}:{1}:[0-9]{{1,20}}$' -f $Endpoint.ProcessId,$Endpoint.ProcessStart)){
        throw 'COM window activation requires the active native lease and an attested model name.'
    }

    $target=$Session.GetModelFromFileName([string]$Request.TargetModel)
    if($null -eq $target -or [string]$target.FileName -ine $Request.TargetModel){throw 'The attested model is no longer loaded.'}
    if($Request.TargetPath -and ([IO.Path]::GetFullPath([string]$target.Origin) -ine $Request.TargetPath)){
        throw 'The loaded Creo file origin does not match the exact native handoff.'
    }
    $window=$null
    try{$window=$Session.GetModelWindow($target)}
    catch{if($_.Exception.GetBaseException().Message -notmatch '\bXToolkitNotFound\b'){throw}}

    $check=Invoke-BwtlComNative $Endpoint 'COM_CHECK_LEASE' $Request.Lease $Request.TargetModel
    if($check.Code -ne 0){throw 'The selected Creo window handoff expired or changed before COM activation.'}

    if($null -eq $window){
        $window=$Session.CreateModelWindow($target)
        $target.Display()
    }
    if($null -eq $window -or [string]$window.Model.FileName -ine $Request.TargetModel){
        throw 'The Creo window is not attached to the attested target model.'
    }
    $windowId=[int]$window.GetId()
    [void]$window.Activate()
    $clock=[Diagnostics.Stopwatch]::StartNew()
    do{
        $result=Get-BwtlPtcState $Session $false
        if($result.Window -eq $windowId -and $result.Model -ieq $Request.TargetModel){break}
        Start-Sleep -Milliseconds 75
    }while($clock.ElapsedMilliseconds -lt 800)
    Assert-BwtlPtcState $result $Request.ExpectedWorkDir $Request.TargetModel $windowId 'after COM activation'
    return $result
}
