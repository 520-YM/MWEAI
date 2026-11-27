#requires -version 5.1
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CreoComResidentCommon.ps1')
. (Join-Path $PSScriptRoot 'CreoComResidentApi.ps1')
$link=$env:BWTL_COM_LINK_IDENTITY|ConvertFrom-Json
[Environment]::SetEnvironmentVariable('BWTL_COM_LINK_IDENTITY',$null)
$connection=$null
$inputPipe=[Console]::OpenStandardInput()
$outputPipe=[Console]::OpenStandardOutput()

function Send-LinkReply([bool]$Ok,$Result,[string]$Message='') {
    $reply=@{Protocol=$bwtlComProtocol;Ok=$Ok;Result=$Result;Message=$Message}
    Write-BwtlComFrame $outputPipe ($reply|ConvertTo-Json -Compress) 2500
}

try{
    if($link.Bind){
        $id=[string]$link.Bind.ConnectionId
        $expected=[string]$link.Bind.ExpectedModel
        $directory=[string]$link.Bind.ExpectedWorkDir
        $window=$link.Bind.ExpectedWindow
    }else{
        $name=$link.Endpoint.Replace('BWTL.Control.','BWTL.Resident.')
        $native=[IO.Pipes.NamedPipeClientStream]::new('.',$name,[IO.Pipes.PipeDirection]::InOut,[IO.Pipes.PipeOptions]::Asynchronous,
            [Security.Principal.TokenImpersonationLevel]::Identification)
        try{
            $native.Connect(1300)
            $snapshot=Read-BwtlComNativeState $link (Invoke-BwtlComNative $link 'STATUS' '' '' $native)
            $identity=Read-BwtlComNativeState $link (Invoke-BwtlComNative $link 'COM_IDENT' $snapshot.context '' $native)
            if(!$identity.connectId -or $identity.context -cne $snapshot.context){throw 'The native Creo COM identity could not be verified.'}
        }finally{$native.Dispose()}
        $id=$identity.connectId
        $expected=if($identity.model){"$($identity.model).$($identity.type)"}else{''}
        $directory=$identity.workDir
        $window=[int]$identity.window
    }

    $connection=Connect-BwtlPtc $id
    $state=Get-BwtlPtcState $connection.Session (!$expected)
    Assert-BwtlPtcState $state $directory $expected $window
    Send-LinkReply $true @{ConnectionId=$id}

    while($true){
        try{$request=Read-BwtlComFrame $inputPipe ([int]::MaxValue)|ConvertFrom-Json}
        catch [IO.EndOfStreamException]{break}
        if($request.Operation -ceq 'Close'){
            [void]$connection.Disconnect(1)
            $connection=$null
            Send-LinkReply $true $true
            break
        }
        try{
            if($request.Operation -cnotin @('Probe','Activate') -or $request.ConnectionId -cne $id -or
                !$connection.IsRunning() -or [string]$connection.GetConnectionId().ExternalRep -cne $id){
                throw 'The resident PTC connection no longer matches the native Creo identity.'
            }
            $session=$connection.Session
            $result=Get-BwtlPtcState $session (!$request.ExpectedModel)
            Assert-BwtlPtcState $result $request.ExpectedWorkDir $request.ExpectedModel $request.ExpectedWindow ('before COM '+$request.Operation)
            if($request.Operation -ceq 'Activate'){$result=Invoke-BwtlPtcActivation $link $session $request}
            Send-LinkReply $true $result
        }catch{Send-LinkReply $false $null $_.Exception.GetBaseException().Message}
    }
}catch{
    try{Send-LinkReply $false $null $_.Exception.GetBaseException().Message}catch{}
}finally{
    if($connection){try{[void]$connection.Disconnect(1)}catch{}}
    $inputPipe.Dispose()
    $outputPipe.Dispose()
}
