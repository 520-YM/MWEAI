$script:bwtlComProtocol = 'BWTL-COM/1'
$script:bwtlComEncoding = [Text.UTF8Encoding]::new($false,$true)
$script:bwtlComMaxFrame = 65536

function Get-BwtlComRuntime([string]$Profile) {
    $path=[IO.Path]::GetFullPath($Profile).TrimEnd('\')
    if($path -notmatch '(?i)^E:\\[^\\]'){throw 'The BWTL COM profile must be on E:.'}
    $runtime=Join-Path $path 'data\connection-runtime'
    for($dir=$runtime;$dir;$dir=Split-Path -Parent $dir){
        if((Test-Path -LiteralPath $dir) -and ((Get-Item -Force -LiteralPath $dir).Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw 'The BWTL COM profile traverses a reparse point.'
        }
    }
    return $runtime
}

function Get-BwtlComResidentPipeName {
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $session=[Diagnostics.Process]::GetCurrentProcess().SessionId
    return "BWTL.CreoComResident.$sid.$session"
}

function Read-BwtlComBytes($Pipe,[int]$Size,$Clock,[int]$TimeoutMs) {
    $bytes=[byte[]]::new($Size)
    $offset=0
    while($offset -lt $Size){
        $remaining=$TimeoutMs-$Clock.ElapsedMilliseconds
        if($remaining -le 0){throw [TimeoutException]::new('The BWTL COM pipe did not return a complete receipt.')}
        $task=$Pipe.ReadAsync($bytes,$offset,$Size-$offset)
        if(!$task.Wait([int]$remaining)){throw [TimeoutException]::new('The BWTL COM pipe did not return a complete receipt.')}
        $count=$task.GetAwaiter().GetResult()
        if($count -eq 0){throw [IO.EndOfStreamException]::new('The BWTL COM pipe disconnected.')}
        $offset+=$count
    }
    return ,$bytes
}

function Read-BwtlComFrame($Pipe,[int]$TimeoutMs) {
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $size=[BitConverter]::ToUInt32((Read-BwtlComBytes $Pipe 4 $clock $TimeoutMs),0)
    if(!$size -or $size -gt $bwtlComMaxFrame){throw 'Invalid BWTL COM frame length.'}
    $bwtlComEncoding.GetString((Read-BwtlComBytes $Pipe ([int]$size) $clock $TimeoutMs))
}

function Write-BwtlComFrame($Pipe,[string]$Text,[int]$TimeoutMs) {
    $bytes=$bwtlComEncoding.GetBytes($Text)
    if(!$bytes.Length -or $bytes.Length -gt $bwtlComMaxFrame){throw 'Invalid BWTL COM frame length.'}
    $buffer=[byte[]]::new($bytes.Length+4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$bytes.Length),$buffer,4)
    [Array]::Copy($bytes,0,$buffer,4,$bytes.Length)
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $task=$Pipe.WriteAsync($buffer,0,$buffer.Length)
    if(!$task.Wait($TimeoutMs)){throw [TimeoutException]::new('Sending the BWTL COM request timed out.')}
    [void]$task.GetAwaiter().GetResult()
    $remaining=$TimeoutMs-$clock.ElapsedMilliseconds
    if($remaining -le 0){throw [TimeoutException]::new('Sending the BWTL COM request timed out.')}
    $flush=$Pipe.FlushAsync()
    if(!$flush.Wait([int]$remaining)){throw [TimeoutException]::new('Sending the BWTL COM request timed out.')}
    [void]$flush.GetAwaiter().GetResult()
}

function Invoke-BwtlComResidentRequest($Request,[int]$TimeoutMs=15000,[int]$ConnectMs=2500) {
    $pipe=[IO.Pipes.NamedPipeClientStream]::new('.',(Get-BwtlComResidentPipeName),[IO.Pipes.PipeDirection]::InOut,
        [IO.Pipes.PipeOptions]::Asynchronous,[Security.Principal.TokenImpersonationLevel]::Identification)
    try{
        $pipe.Connect($ConnectMs)
        $Request['Protocol']=$bwtlComProtocol
        Write-BwtlComFrame $pipe ($Request|ConvertTo-Json -Compress) 2000
        $reply=Read-BwtlComFrame $pipe $TimeoutMs|ConvertFrom-Json
        if($reply.Protocol -cne $bwtlComProtocol -or $reply.Ok -isnot [bool]){throw 'Invalid BWTL resident COM response.'}
        if(!$reply.Ok){throw $reply.Message}
        return $reply.Result
    }finally{$pipe.Dispose()}
}

function Invoke-BwtlComNative($Endpoint,[string]$Verb,[string]$Context='', [string]$Payload='', $ExistingPipe) {
    $pipe=$ExistingPipe
    if(!$pipe){
        $pipe=[IO.Pipes.NamedPipeClientStream]::new('.',$Endpoint.Endpoint,[IO.Pipes.PipeDirection]::InOut,
            [IO.Pipes.PipeOptions]::Asynchronous,[Security.Principal.TokenImpersonationLevel]::Identification)
    }
    try{
        if(!$ExistingPipe){$pipe.Connect(1300)}
        Write-BwtlComFrame $pipe (@('BWTL/1',$Verb,$Context,$Payload)-join "`n") 1500
        $parts=(Read-BwtlComFrame $pipe 2500)-csplit "`n",3
        $code=0
        if($parts.Count -ne 3 -or $parts[0] -cne 'BWTL/1' -or ![int]::TryParse($parts[1],[ref]$code)){
            throw 'Invalid BWTL native COM handoff receipt.'
        }
        [pscustomobject]@{Code=$code;Body=$parts[2]}
    }finally{if(!$ExistingPipe){$pipe.Dispose()}}
}

function Read-BwtlComNativeState($Endpoint,$Reply) {
    if($Reply.Code -ne 0){throw "The BWTL native endpoint is unavailable (code $($Reply.Code))."}
    $fields=@{}
    foreach($line in ($Reply.Body -csplit "`n")){
        $field=$line -csplit '=',2
        if($field.Count -ne 2 -or !$field[0] -or $fields.ContainsKey($field[0])){throw 'Invalid BWTL native COM identity receipt.'}
        $fields[$field[0]]=$field[1].TrimEnd([char]13)
    }
    $binding='^{0}:{1}:[0-9]+$' -f $Endpoint.ProcessId,$Endpoint.ProcessStart
    if($fields.pid -cne [string]$Endpoint.ProcessId -or $fields.processStart -cne $Endpoint.ProcessStart -or
        $fields.context -cnotmatch $binding){throw 'The BWTL native connection identity changed.'}
    return $fields
}
