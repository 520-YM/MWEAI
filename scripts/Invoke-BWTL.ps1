
#requires -version 5.1
[CmdletBinding()]
param(
    [ValidateSet('List','Status','Connect','Navigate','Read','Execute','Save','CreatePair','PressFit','BuildPanel','RevisePanel','ListPressfitHoles','GroupPressfit','RevisePressfitGroup','PressfitWorkspace','OpenModel','OpenBackground','SwitchModel','RebuildModel','ListModels','Handoff','TaskState','Watch','ReferenceDesign')]
    [string]$Action='List',
    [ValidateRange(1,32767)]
    [int]$Session,
    [string]$ExpectedContext,
    [Alias('Request')]
    [string]$Command,
    [string]$InstallRoot,
    [string[]]$ProfileRoot,
    [ValidateRange(500,120000)]
    [int]$TimeoutMs=60000,
    [ValidateRange(0,[long]::MaxValue)]
    [long]$AfterSequence=0,
    [ValidateRange(1,60)]
    [int]$WatchSeconds=15,
    [switch]$Json
)

$ErrorActionPreference='Stop'
$protocol='BWTL/1'
$maximumFrame=65536
$utf8=[Text.UTF8Encoding]::new($false,$true)

function Get-LocalPath([string]$Path) {
    $absolute=[IO.Path]::GetFullPath($Path).TrimEnd('\')
    if($absolute -notmatch '(?i)^E:\\[^\\]'){
        throw 'BWTL 会话目录须在本机 E 盘。'
    }
    for($parent=$absolute;$parent;$parent=Split-Path -Parent $parent){
        if((Test-Path -LiteralPath $parent) -and
            ((Get-Item -Force -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw 'BWTL 会话路径包含符号链接或联接点。'
        }
    }
    return $absolute
}

function Get-ProfileRoots {
    if($ProfileRoot){
        foreach($root in $ProfileRoot){ Get-LocalPath $root }
        return
    }

    $root=if($InstallRoot){$InstallRoot}else{
        $configured=[Environment]::GetEnvironmentVariable('BWTL_INSTALL_ROOT','User')
        if($configured){$configured}else{'E:\BWTL-Data\AppData\Local\BWTL'}
    }
    $root=Get-LocalPath $root
    $root
    $instances=Get-LocalPath ($root+'-Instances')
    if(Test-Path -LiteralPath $instances -PathType Container){
        foreach($folder in Get-ChildItem -Directory -LiteralPath $instances){
            if($folder.Name -match '^[1-9][0-9]{0,4}$'){ Get-LocalPath $folder.FullName }
        }
    }
}

function Get-Endpoint([string]$Root) {
    $file=Get-LocalPath (Join-Path $Root 'data\local-control.endpoint')
    if(!(Test-Path -LiteralPath $file -PathType Leaf)){return}

    try {
        if((Get-Item -LiteralPath $file).Length -gt 2048){return}
        $lines=[IO.File]::ReadAllLines($file,$utf8)
        if($lines.Count -ne 5 -or $lines[0] -cne $protocol -or
            $lines[2] -notmatch '^[1-9][0-9]{0,9}$' -or
            $lines[3] -notmatch '^[1-9][0-9]{0,18}$' -or
            $lines[4] -notmatch '^[1-9][0-9]{0,4}$'){return}
        $identity="BWTL.Control.$($lines[2]).$($lines[3]).$($lines[4])"
        if($lines[1] -cne $identity){return}
        $processId=[int]$lines[2]
        $processStart=[long]$lines[3]
        $slot=[int]$lines[4]
        if($slot -gt 32767){return}
        $process=Get-Process -Id $processId -ErrorAction Stop
        if($process.ProcessName -ine 'xtop' -or $process.SessionId -ne (Get-Process -Id $PID).SessionId -or
            $process.StartTime.ToFileTimeUtc() -ne $processStart -or $process.HasExited){return}
        [pscustomobject]@{Session=$slot;ProcessId=$processId;ProcessStart=$lines[3];Endpoint=$identity;ProfileRoot=$Root}
    } catch {
        Write-Verbose "会话记录已过期或正在更新，跳过 $file"
    }
}

function Get-RemainingTime([Diagnostics.Stopwatch]$Clock,[int]$LimitMs=$TimeoutMs) {
    $remaining=$LimitMs-$Clock.ElapsedMilliseconds
    if($remaining -le 0){throw [TimeoutException]::new('等待 BWTL 响应超时。')}
    return [int]$remaining
}

function Read-Exact($Pipe,[int]$Size,[Diagnostics.Stopwatch]$Clock,[int]$LimitMs=$TimeoutMs) {
    $data=[byte[]]::new($Size)
    $received=0
    while($received -lt $Size){
        $wait=Get-RemainingTime $Clock $LimitMs
        $task=$Pipe.ReadAsync($data,$received,$Size-$received,[Threading.CancellationToken]::None)
        if(!$task.Wait($wait)){throw [TimeoutException]::new('读取 BWTL 回执超时。')}
        $chunk=$task.GetAwaiter().GetResult()
        if($chunk -eq 0){throw [IO.EndOfStreamException]::new('BWTL 未返回完整回执。')}
        $received+=$chunk
    }
    return ,$data
}

function New-BwtlClient($Endpoint,[ValidateSet('Control','Resident','Events')][string]$Channel='Control') {
    $name=$Endpoint.Endpoint -creplace '^BWTL\.Control\.',("BWTL.$Channel.")
    [IO.Pipes.NamedPipeClientStream]::new('.',$name,[IO.Pipes.PipeDirection]::InOut,
        [IO.Pipes.PipeOptions]::Asynchronous,[Security.Principal.TokenImpersonationLevel]::Identification)
}

function Send-BwtlRequest($Pipe,$Clock,[string]$Verb,[string]$Context,[string]$Payload) {
    $request=$utf8.GetBytes(@($protocol,$Verb,$Context,$Payload) -join [char]10)
    if($request.Length -gt $maximumFrame){throw '本次提交的文字过长。'}
    $frame=[byte[]]::new($request.Length+4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$request.Length),0,$frame,0,4)
    [Array]::Copy($request,0,$frame,4,$request.Length)

    $task=$Pipe.WriteAsync($frame,0,$frame.Length,[Threading.CancellationToken]::None)
    if(!$task.Wait((Get-RemainingTime $Clock))){throw [TimeoutException]::new('提交 BWTL 指令超时。')}
    [void]$task.GetAwaiter().GetResult()
}

function Read-BwtlResponse($Pipe,$Clock,[int]$LimitMs=$TimeoutMs) {
    $length=[BitConverter]::ToUInt32((Read-Exact $Pipe 4 $Clock $LimitMs),0)
    if($length -eq 0 -or $length -gt $maximumFrame){throw 'BWTL 回执长度无效。'}
    $response=$utf8.GetString((Read-Exact $Pipe ([int]$length) $Clock $LimitMs))
    $parts=$response -csplit [char]10,3
    $code=0
    if($parts.Count -ne 3 -or $parts[0] -cne $protocol -or ![int]::TryParse($parts[1],[ref]$code)){
        throw 'BWTL 回执格式无效。'
    }
    [pscustomobject]@{Code=$code;Body=$parts[2]}
}

function Invoke-BwtlEndpoint($Endpoint,[string]$Verb,[string]$Context,[string]$Payload,$PersistentPipe) {
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $pipe=if($PersistentPipe){$PersistentPipe}else{New-BwtlClient $Endpoint}
    $transmitting=$false
    try{
        if(!$PersistentPipe){$pipe.Connect((Get-RemainingTime $clock))}
        $transmitting=$true
        Send-BwtlRequest $pipe $clock $Verb $Context $Payload
        $response=Read-BwtlResponse $pipe $clock
    }catch{
        $reason=$_.Exception.GetBaseException().Message
        if($transmitting -and $Verb -in @('EXEC','SAVE','CREATE_PAIR','PRESSFIT','BUILD_PANEL','REV_PANEL','GROUP_FIT','REV_GROUP','FIT_WORK','OPEN_MODEL','SWITCH_MODEL','REBUILD_MODEL','HANDOFF','REFERENCE_DESIGN')){
            throw "没有拿到写入回执。请先重新读取该会话状态并核对结果，再决定是否继续。$reason"
        }
        throw "无法取得 Creo 会话 $($Endpoint.Session) 的 BWTL 回执。$reason"
    }finally{
        if(!$PersistentPipe){$pipe.Dispose()}
    }

    [pscustomobject]@{
        Session=$Endpoint.Session
        ProcessId=$Endpoint.ProcessId
        ProcessStart=$Endpoint.ProcessStart
        Endpoint=$Endpoint.Endpoint
        Action=$Action
        Code=$response.Code
        Body=$response.Body
        Data=$null
    }
}

function Read-EndpointFields($Result,[string[]]$Required) {
    $data=[ordered]@{}
    foreach($line in $Result.Body -csplit [char]10){
        $pair=$line -csplit '=',2
        if($pair.Count -ne 2 -or !$pair[0] -or $data.Contains($pair[0])){throw 'BWTL 状态字段无法解析。'}
        $data[$pair[0]]=$pair[1].TrimEnd([char]13)
    }
    foreach($name in $Required){
        if(!$data.Contains($name)){throw "BWTL 状态缺少 $name。"}
    }
    if($data.pid -cne [string]$Result.ProcessId -or $data.processStart -cne $Result.ProcessStart){
        throw 'BWTL 回读的进程身份与选定会话不一致。'
    }
    return $data
}

function Read-StatusFields($Result) {
    $data=Read-EndpointFields $Result @('pid','processStart','window','model','type','subtype','workDir','stamp','context')
    if($data.context -cnotmatch "^$($Result.ProcessId):$($Result.ProcessStart):[0-9]{1,20}$"){
        throw 'BWTL 返回的操作绑定与选定会话不一致。'
    }
    $Result.Data=[pscustomobject]$data
}

function Get-LiveJournal($Endpoint,$Result) {
    $data=Read-EndpointFields $Result @('pid','processStart','slot')
    if($data.slot -cne [string]$Endpoint.Session){throw 'BWTL 回读的会话编号不一致。'}
    if(!$data.liveJournal){throw '当前 BWTL 会话尚未提供实时记录。'}
    if($data.liveJournal -notmatch '(?i)^E:\\[^\\]'){throw 'BWTL 返回了无效的实时记录路径。'}

    $journal=Get-LocalPath $data.liveJournal
    $directory=Split-Path -Parent $journal
    $archiveRoot=Get-LocalPath (Join-Path $Endpoint.ProfileRoot 'data\聊天记录')
    $processSuffix='_PID'+$Endpoint.ProcessId+'_'+([long]$Endpoint.ProcessStart).ToString('x16')
    $folder=Split-Path -Leaf $directory
    if((Split-Path -Parent $directory) -ine $archiveRoot -or
        (Split-Path -Leaf $journal) -ine 'live-events.jsonl' -or
        $folder -cnotmatch ('^[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{3}'+[regex]::Escape($processSuffix)+'$')){
        throw 'BWTL 实时记录路径与选定的 Creo 进程不一致。'
    }
    return $journal
}

function Confirm-LiveProcess($Endpoint) {
    $process=Get-Process -Id $Endpoint.ProcessId -ErrorAction Stop
    try {
        if($process.ProcessName -ine 'xtop' -or $process.HasExited -or
            $process.StartTime.ToFileTimeUtc() -ne [long]$Endpoint.ProcessStart){
            throw '原 Creo 会话已结束，实时读取已停止。'
        }
    } finally { $process.Dispose() }
}

function Read-JournalBatch([IO.Stream]$Stream,[byte[]]$Buffer,[switch]$SkipInitialPartialLine) {
    $offset=$Stream.Position
    $pending=$Stream.Length-$offset
    if($pending -lt 0){throw '实时记录已被截断，请重新读取 TaskState。'}
    if(!$pending){return}
    $count=[int][Math]::Min($Buffer.Length,$pending)
    $length=0
    while($length -lt $count){
        $read=$Stream.Read($Buffer,$length,$count-$length)
        if(!$read){break}
        $length+=$read
    }
    if(!$length){return}
    $end=[Array]::LastIndexOf($Buffer,[byte]10,$length-1)
    if($end -lt 0){
        $Stream.Position=$offset
        if($length -gt 32768){throw '实时记录包含超长事件。'}
        return
    }
    $Stream.Position=$offset+$end+1
    $lineStart=0
    if($SkipInitialPartialLine){$lineStart=[Array]::IndexOf($Buffer,[byte]10,0,$length)+1}

    while($lineStart -lt $end){
        $lineEnd=[Array]::IndexOf($Buffer,[byte]10,$lineStart,$end-$lineStart+1)
        $lineLength=$lineEnd-$lineStart
        if($lineLength -lt 1 -or $lineLength -gt 32768){throw '实时记录事件长度有误。'}
        $line=$utf8.GetString($Buffer,$lineStart,$lineLength).TrimEnd([char]13)
        if(!$offset -and !$lineStart){$line=$line.TrimStart([char]0xFEFF)}
        $event=ConvertFrom-Json -InputObject $line
        if($null -eq $event -or ($event.seq -isnot [int] -and $event.seq -isnot [long]) -or $event.seq -lt 1 -or
            ($event.task -isnot [int] -and $event.task -isnot [long]) -or $event.task -lt 0 -or
            $event.kind -cnotin @('user','request','progress','receipt','model')){
            throw '实时记录包含无效的事件编号或类型。'
        }
        $event
        $lineStart=$lineEnd+1
    }
}

function New-BoundJournalStream([string]$Journal) {
    if(!(Test-Path -LiteralPath $Journal -PathType Leaf)){return}
    if((Get-LocalPath $Journal) -ine $Journal){throw 'BWTL 实时记录路径已变更。'}
    return [IO.File]::Open($Journal,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
}

function Read-LiveTaskState([string]$Journal,$Result) {
    $stream=New-BoundJournalStream $Journal
    $limit=2MB
    $truncated=$false
    $sequence=[long]0
    $state=[ordered]@{LatestSequence=$sequence;HistoryTruncated=$truncated;LatestTask=$null;LatestUser=$null;
        LatestRequest=$null;LatestProgress=$null;LatestReceipt=$null;LatestNativeReceipt=$null;LatestModel=$null;NeedsHandoff=$true}
    if($stream){
        try{
            $truncated=$stream.Length -gt $limit
            if($truncated){$stream.Position=$stream.Length-$limit}
            $state.HistoryTruncated=$truncated
            $events=@(Read-JournalBatch $stream ([byte[]]::new($limit)) -SkipInitialPartialLine:$truncated)
            foreach($event in $events){
                if($event.seq -le $sequence){throw '实时记录的事件顺序有误。'}
                $sequence=[long]$event.seq
                if($event.kind -ceq 'model'){$state.LatestModel=$event}
                if($event.task -gt 0 -and ($null -eq $state.LatestTask -or $event.task -gt $state.LatestTask)){
                    $state.LatestTask=$event.task
                    foreach($name in 'LatestUser','LatestRequest','LatestProgress','LatestReceipt','LatestNativeReceipt'){$state[$name]=$null}
                }
                if($null -eq $state.LatestTask -or $event.task -ne $state.LatestTask){continue}
                switch($event.kind){
                    'user'     {$state.LatestUser=$event}
                    'request'  {$state.LatestRequest=$event}
                    'progress' {$state.LatestProgress=$event}
                    'receipt' {
                        $state.LatestReceipt=$event
                        if($event.code -is [int] -or $event.code -is [long]){$state.LatestNativeReceipt=$event}
                    }
                }
            }
            $state.LatestSequence=$sequence
            $workDir=([string]$state.LatestModel.workDir).TrimEnd('\','/')
            foreach($name in 'LatestUser','LatestRequest','LatestProgress','LatestReceipt','LatestNativeReceipt'){
                if(!$workDir -or ([string]$state[$name].workDir).TrimEnd('\','/') -ine $workDir){$state[$name]=$null}
            }
            $state.NeedsHandoff=!($state.LatestUser -or $state.LatestRequest)
        }finally{$stream.Dispose()}
    }
    $Result.Body=''
    $Result.Data=[pscustomobject]$state
}

function Watch-LiveJournal([string]$Journal,$Endpoint) {
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $sequence=$AfterSequence
    $processCheck=0
    $stream=$null
    $buffer=[byte[]]::new(2MB)
    $skipInitialPartialLine=$false
    try{
        while($clock.Elapsed.TotalSeconds -lt $WatchSeconds){
            if($clock.ElapsedMilliseconds -ge $processCheck){
                Confirm-LiveProcess $Endpoint
                $processCheck=$clock.ElapsedMilliseconds+1000
            }
            if(!$stream){
                $stream=New-BoundJournalStream $Journal
                if($stream -and $stream.Length -gt $buffer.Length){
                    $stream.Position=$stream.Length-$buffer.Length
                    $skipInitialPartialLine=$true
                }
            }
            if($stream){
                $events=@(Read-JournalBatch $stream $buffer -SkipInitialPartialLine:$skipInitialPartialLine)
                if($skipInitialPartialLine -and $events.Count){
                    $skipInitialPartialLine=$false
                    if($sequence -lt $events[0].seq-1){
                        throw '指定的事件序号早于当前读取范围。请先查看 TaskState，再按最新序号继续。'
                    }
                }
                foreach($event in $events){
                    if($event.seq -le $sequence){continue}
                    $sequence=[long]$event.seq
                    if($Json){ConvertTo-Json -InputObject $event -Depth 5 -Compress}
                    else{$event}
                }
            }
            Start-Sleep -Milliseconds 100
        }
    }finally{
        if($stream){$stream.Dispose()}
    }
}

try{
    if(![IO.Directory]::Exists('E:\')){throw 'E 盘当前不可用。'}
    foreach($field in @($ExpectedContext,$Command)){
        if($field -match '[\r\n\x00]'){throw '本次只接受一条单行指令。'}
    }
    if($Action -in @('List','Status','Connect','TaskState','Watch') -and ($ExpectedContext -or $Command)){
        throw '列出会话、查看状态和实时记录不接收指令或绑定参数。'
    }
    if($Action -ne 'Watch' -and ($PSBoundParameters.ContainsKey('WatchSeconds') -or $PSBoundParameters.ContainsKey('AfterSequence'))){
        throw '事件序号和监看时长只用于 Watch。'
    }
    if($Action -eq 'Save' -and $Command){throw '保存使用已绑定的当前模型，不接收路径或指令。'}
    if($Action -in @('ListModels','RebuildModel') -and $Command){throw '本操作使用已绑定的 Creo 会话，不接收额外指令。'}
    if($Action -eq 'ListPressfitHoles' -and $Command){throw '查看孔位不接收额外指令。'}
    if($Action -in @('Execute','CreatePair','PressFit','BuildPanel','RevisePanel','GroupPressfit','RevisePressfitGroup','OpenModel','OpenBackground','SwitchModel','Handoff','ReferenceDesign') -and [string]::IsNullOrWhiteSpace($Command)){throw '请给出本次操作的参数。'}
    if($Action -in @('Execute','Save','CreatePair','PressFit','BuildPanel','RevisePanel','GroupPressfit','RevisePressfitGroup','ListPressfitHoles','PressfitWorkspace','OpenModel','OpenBackground','SwitchModel','RebuildModel','ListModels','Handoff','ReferenceDesign') -and !$ExpectedContext){throw '请先读取 Status 取得当前会话的 context，再提交操作。'}
    if($Action -eq 'Navigate' -and (!$ExpectedContext -or [string]::IsNullOrWhiteSpace($Command))){
        throw '通过 COM 切换窗口需要当前 Status 的 context 和明确的目标指令。'
    }

    $endpoints=@(Get-ProfileRoots | Sort-Object -Unique | ForEach-Object {Get-Endpoint $_} |
        Sort-Object -Property Session,ProcessId)
    if($Action -eq 'List'){
        if($Json){ ConvertTo-Json -InputObject @($endpoints) -Depth 4 }
        else{ $endpoints }
        exit 0
    }

    $found=@(if($PSBoundParameters.ContainsKey('Session')){$endpoints | Where-Object Session -eq $Session}else{$endpoints})
    if(!$found.Count){throw '找不到指定的 BWTL 会话，请查看 List，或正常打开已更新的 Creo。'}
    if($found.Count -ne 1){throw '找到多个 BWTL 会话，请明确指定唯一会话编号。'}
    $endpoint=$found[0]
    if($ExpectedContext -and $ExpectedContext -cnotmatch "^$($endpoint.ProcessId):$($endpoint.ProcessStart):[0-9]{1,20}$"){
        throw '绑定来自其他 Creo 进程或该会话已重启，请重新读取 Status。'
    }

    if($Action -in @('Connect','Navigate','Watch')){
        . (Join-Path $PSScriptRoot 'connection\BwtlFusion.ps1')
    }
    if($Action -in @('Connect','Navigate')){
        $hello=Invoke-BwtlEndpoint $endpoint 'HELLO' '' ''
        if($hello.Code){throw $hello.Body}
        $capabilities=Read-EndpointFields $hello @('pid','processStart','slot')
        if($capabilities.resident -ne '1'){throw '该 Creo 进程尚未加载三路连接版 BWTL。'}
        $result=Invoke-BwtlFusion $endpoint $Action $ExpectedContext $Command
        if($Json){ConvertTo-Json -InputObject $result -Depth 6}else{$result}
        if($result.Code){exit 1}
        exit 0
    }

    if($Action -in @('TaskState','Watch')){
        $result=Invoke-BwtlEndpoint $endpoint 'HELLO' '' ''
        if($result.Code){throw "无法确认 Creo 会话。$($result.Body)"}
        $capabilities=Read-EndpointFields $result @('pid','processStart','slot')
        if($Action -eq 'Watch' -and $capabilities.resident -eq '1'){
            $cursor=if($PSBoundParameters.ContainsKey('AfterSequence')){[string]$AfterSequence}else{''}
            Watch-BwtlResident $endpoint $cursor
        }else{
            $journal=Get-LiveJournal $endpoint $result
            if($Action -eq 'Watch'){
                Watch-LiveJournal $journal $endpoint
            }else{
                Confirm-LiveProcess $endpoint
                Read-LiveTaskState $journal $result
                if($Json){ConvertTo-Json -InputObject $result -Depth 6}
                else{$result}
            }
        }
        exit 0
    }

    $verb=switch($Action){
        'Status' {'STATUS'}
        'Read'    {'READ'}
        'Execute' {'EXEC'}
        'Save'    {'SAVE'}
        'CreatePair' {'CREATE_PAIR'}
        'PressFit'   {'PRESSFIT'}
        'BuildPanel' {'BUILD_PANEL'}
        'RevisePanel' {'REV_PANEL'}
        'ListPressfitHoles' {'GROUP_HOLES'}
        'GroupPressfit' {'GROUP_FIT'}
        'RevisePressfitGroup' {'REV_GROUP'}
        'PressfitWorkspace' {'FIT_WORK'}
        'OpenModel' {'OPEN_MODEL'}
        'OpenBackground' {'OPEN_BACKGROUND'}
        'SwitchModel' {'SWITCH_MODEL'}
        'RebuildModel' {'REBUILD_MODEL'}
        'ReferenceDesign' {'REFERENCE_DESIGN'}
        'ListModels' {'LIST_MODELS'}
        'Handoff' {'HANDOFF'}
    }
    $result=Invoke-BwtlEndpoint $endpoint $verb $ExpectedContext $Command
    if($Action -eq 'Status' -and !$result.Code){Read-StatusFields $result}
    if($Json){ ConvertTo-Json -InputObject $result -Depth 5 }
    else{ $result }
    if($result.Code){exit 1}
    exit 0
}catch{
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 2
}
