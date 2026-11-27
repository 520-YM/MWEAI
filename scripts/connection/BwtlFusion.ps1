function Get-BwtlTypedModel($Fields) {
    if($Fields.type -eq 'NONE' -and !$Fields.model){return ''}
    if($Fields.type -notin @('PRT','ASM') -or !$Fields.model -or $Fields.windowModel -ine $Fields.model){
        throw '先结束正在进行的组件就地编辑，再通过 COM 切换独立的零件或装配窗口。'
    }
    return "$($Fields.model).$($Fields.type)"
}

function Read-BwtlFusionFields($Result,[string[]]$Required) {
    if($Result.Code){throw $Result.Body}
    $fields=Read-EndpointFields $Result (@('window','model','type','workDir','context','windowModel')+$Required)
    if($fields.context -cnotmatch "^$($Result.ProcessId):$($Result.ProcessStart):[0-9]{1,20}$"){
        throw '实时连接返回的绑定与选定的 Creo 进程不一致。'
    }
    return $fields
}

function Invoke-BwtlFusion($Endpoint,[string]$Operation,[string]$Context,[string]$Command) {
    . (Join-Path $PSScriptRoot 'CreoCom.ps1')
    $pipe=New-BwtlClient $Endpoint Resident
    $lease=''
    try{
        $clock=[Diagnostics.Stopwatch]::StartNew()
        $pipe.Connect((Get-RemainingTime $clock))
        $before=Invoke-BwtlEndpoint $Endpoint 'STATUS' '' '' $pipe
        $native=Read-BwtlFusionFields $before @()
        if($Operation -eq 'Connect'){
            $hello=Invoke-BwtlEndpoint $Endpoint 'COM_IDENT' $native.context '' $pipe
            $fields=Read-BwtlFusionFields $hello @('connectId')
            $com=Invoke-BwtlCreoCom -Endpoint $Endpoint -Operation Probe -ConnectionId $fields.connectId `
                -ExpectedWorkDir $fields.workDir -ExpectedModel (Get-BwtlTypedModel $fields) `
                -ExpectedWindow ([int]$fields.window) -TimeoutSeconds 6 `
                -RequireResident:($fields.comStartup -in @('starting','ready','connected','unavailable'))
            $after=Invoke-BwtlEndpoint $Endpoint 'STATUS' '' '' $pipe
            if($after.Code){throw $after.Body}
            Read-StatusFields $after
            if($after.Data.context -cne $fields.context){
                throw 'Creo 会话在双向核对期间发生变化。请重新读取状态。'
            }
            $after | Add-Member -NotePropertyName Connection -NotePropertyValue ([pscustomobject]@{
                Resident='BWTL.Resident'; Navigation='Creo VB API ConnectById'; Native='BWTL.Control';
                Model=$com.Model; Window=$com.Window; WorkDir=$com.WorkDir; ComMode=$com.Mode
            })
            return $after
        }

        if($native.context -cne $Context){throw '模型或工作目录已变化。请重新读取本会话状态，再提交切换。'}
        if($Command -cmatch '^ACTIVATE (?<target>[^ ]+)$'){$payload='SWITCH '+$Matches.target}
        elseif($Command -cmatch '^OPEN (?<target>.+)$'){$payload='OPEN '+$Matches.target}
        else{throw '窗口切换使用 ACTIVATE 名称.PRT 或 ACTIVATE 名称.ASM。打开指定文件使用 OPEN 文件名。'}
        $prepare=Invoke-BwtlEndpoint $Endpoint 'COM_PREPARE' $Context $payload $pipe
        $fields=Read-BwtlFusionFields $prepare @('lease','connectId','targetModel','targetType','targetWindow','targetPath')
        $lease=$fields.lease
        if(!$lease -or $fields.targetType -notin @('PRT','ASM') -or !$fields.targetModel){throw 'BWTL 尚未核对要切换的 Creo 模型。'}
        $target="$($fields.targetModel).$($fields.targetType)"
        $com=Invoke-BwtlCreoCom -Endpoint $Endpoint -Operation Activate -ConnectionId $fields.connectId `
            -ExpectedWorkDir $fields.workDir -ExpectedModel (Get-BwtlTypedModel $fields) `
            -ExpectedWindow ([int]$fields.window) -TargetModel $target -TargetPath $fields.targetPath -TimeoutSeconds 6 `
            -Lease $lease -RequireResident:($fields.comStartup -in @('starting','ready','connected','unavailable'))
        $receipt=Invoke-BwtlEndpoint $Endpoint 'COM_FINISH' '' $lease $pipe
        $lease=''
        if($receipt.Code){return $receipt}
        Read-StatusFields $receipt
        if((Get-BwtlTypedModel $receipt.Data) -ine $target -or $receipt.Data.window -ne $com.Window -or
            $receipt.Data.workDir.TrimEnd('\','/') -ine $com.WorkDir.TrimEnd('\','/')){
            throw 'COM 与 BWTL 回读的模型、窗口或工作目录不同，请核对活动窗口后继续。'
        }
        $receipt | Add-Member -NotePropertyName Connection -NotePropertyValue ([pscustomobject]@{
            Resident='BWTL.Resident'; Navigation='Creo VB API ConnectById'; Native='BWTL.Control';
            Model=$com.Model; Window=$com.Window; WorkDir=$com.WorkDir; ComMode=$com.Mode
        })
        return $receipt
    }finally{
        if($lease){
            try{[void](Invoke-BwtlEndpoint $Endpoint 'COM_CANCEL' '' $lease $pipe)}catch{ Write-Verbose 'COM 切换预约将由 Creo 会话自动释放。' }
        }
        $pipe.Dispose()
    }
}

function Watch-BwtlResident($Endpoint,[string]$Cursor) {
    $pipe=New-BwtlClient $Endpoint Events
    try{
        $startup=[Diagnostics.Stopwatch]::StartNew()
        $pipe.Connect((Get-RemainingTime $startup))
        Send-BwtlRequest $pipe $startup 'SUBSCRIBE' '' $Cursor
        $ack=Read-BwtlResponse $pipe $startup
        if($ack.Code){throw $ack.Body}
        if($ack.Body -notmatch '^stream=1\r?\nafterSequence=([0-9]+)\s*$'){throw '实时订阅确认格式无效。'}
        $sequence=[long]$Matches[1]
        $clock=[Diagnostics.Stopwatch]::StartNew()
        $duration=$WatchSeconds*1000
        while($clock.ElapsedMilliseconds -lt $duration){
            try{$frame=Read-BwtlResponse $pipe $clock $duration}
            catch [TimeoutException]{if($clock.ElapsedMilliseconds -ge $duration-1){break};throw}
            if($frame.Code){throw $frame.Body}
            $event=ConvertFrom-Json -InputObject $frame.Body
            if($event.kind -ceq 'gap'){throw '连接期间的事件超出了实时缓冲范围。请从 TaskState 核对记录后继续监看。'}
            if(!$event -or ($event.seq -isnot [int] -and $event.seq -isnot [long]) -or $event.seq -le $sequence -or
                $event.kind -cnotin @('user','request','progress','receipt','model')){throw '收到无效的实时事件。'}
            $sequence=[long]$event.seq
            if($Json){ConvertTo-Json -InputObject $event -Depth 5 -Compress}
            else{$event}
        }
    }finally{$pipe.Dispose()}
}
