#requires -version 5.1
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CreoComResidentCommon.ps1')

function New-BwtlComRegistration($Request) {
    if($Request.ProcessId -notmatch '^[1-9][0-9]{0,9}$' -or $Request.ProcessStart -notmatch '^[1-9][0-9]{1,18}$' -or
        $Request.Slot -notmatch '^[1-9][0-9]{0,4}$' -or [int]$Request.Slot -gt 32767){throw 'Invalid BWTL COM process identity.'}
    $profile=[IO.Path]::GetFullPath([string]$Request.ProfileRoot).TrimEnd('\')
    $runtime=Get-BwtlComRuntime $profile
    $lines=[IO.File]::ReadAllLines((Join-Path $profile 'data\local-control.endpoint'),$bwtlComEncoding)
    $key='{0}.{1}.{2}' -f $Request.ProcessId,$Request.ProcessStart,$Request.Slot
    $nativePipe="BWTL.Control.$key"
    if($lines.Count -ne 5 -or $lines[0] -cne 'BWTL/1' -or $lines[1] -cne $nativePipe -or
        $lines[2] -cne [string]$Request.ProcessId -or $lines[3] -cne $Request.ProcessStart -or
        $lines[4] -cne [string]$Request.Slot){throw 'The selected BWTL native endpoint no longer exists.'}
    $process=Get-Process -Id ([int]$Request.ProcessId) -ErrorAction Stop
    $signal=$null
    try{
        if($process.ProcessName -ine 'xtop' -or $process.HasExited -or $process.SessionId -ne [Diagnostics.Process]::GetCurrentProcess().SessionId -or
            [string]$process.StartTime.ToFileTimeUtc() -cne $Request.ProcessStart){throw 'The selected Creo process is unavailable.'}
        $objects=Split-Path -Parent $process.MainModule.FileName
        if($objects -notmatch '(?i)\\Common Files\\x86e_win64\\obj$'){throw 'The selected Creo 64-bit installation path is unavailable.'}
        $vendor=Join-Path $objects 'pfclscom.exe'
        $comm=Join-Path $objects 'pro_comm_msg.exe'
        if(!(Test-Path -LiteralPath $vendor -PathType Leaf) -or !(Test-Path -LiteralPath $comm -PathType Leaf)){
            throw 'The selected Creo installation lacks its PTC VB API runtime.'
        }
        if($Request.StopEvent){
            if($Request.StopEvent -cne "Local\BWTL.ComStop.$key"){throw 'Invalid BWTL COM lifetime event.'}
            $signal=[Threading.EventWaitHandle]::OpenExisting([string]$Request.StopEvent)
        }
        [void][IO.Directory]::CreateDirectory($runtime)
        return @{Key=$key;ProcessId=[int]$Request.ProcessId;ProcessStart=[string]$Request.ProcessStart;Slot=[int]$Request.Slot;
            Runtime=$runtime;Profile=$profile;Process=$process;StopSignal=$signal;Vendor=$vendor;Communication=$comm;Endpoint=$nativePipe;
            Marker=(Join-Path $runtime ('resident-{0}-{1}.json' -f $Request.ProcessId,$Request.ProcessStart));
            Worker=$null;ConnectionId='';Attempts=0;RetryAt=[datetime]::UtcNow;LastError=''}
    }catch{
        if($signal){$signal.Dispose()}
        $process.Dispose()
        throw
    }
}

function Write-BwtlComRegistration($Link,[string]$State,[string]$Message='') {
    if($Message.Length -gt 600){$Message=$Message.Substring(0,600)}
    $owner=if($Link.Worker){$Link.Worker}else{[Diagnostics.Process]::GetCurrentProcess()}
    $mark=[ordered]@{Pid=$Link.ProcessId;ProcessStart=$Link.ProcessStart;Slot=$Link.Slot;State=$State;
        WorkerPid=$owner.Id;WorkerStart=[string]$owner.StartTime.ToFileTimeUtc();Message=$Message}
    $temp="$($Link.Marker).$PID.tmp"
    [IO.File]::WriteAllText($temp,($mark|ConvertTo-Json -Compress),$bwtlComEncoding)
    if([IO.File]::Exists($Link.Marker)){
        $previous="$temp.previous"
        [IO.File]::Replace($temp,$Link.Marker,$previous)
        [IO.File]::Delete($previous)
    }else{[IO.File]::Move($temp,$Link.Marker)}
}

function Stop-BwtlComLinkWorker($Link,[bool]$RequireClean=$false,[bool]$Abandon=$false) {
    $worker=$Link.Worker
    $Link.Worker=$null
    if(!$worker){return}
    try{
        if(!$worker.HasExited -and !$Abandon){
            try{
                Write-BwtlComFrame $worker.StandardInput.BaseStream '{"Operation":"Close"}' 500
                $back=Read-BwtlComFrame $worker.StandardOutput.BaseStream 2200|ConvertFrom-Json
                if($back.Protocol -cne $bwtlComProtocol -or !$back.Ok){throw 'PTC did not verify detaching the previous Creo process.'}
            }catch{if($RequireClean){throw 'PTC did not verify detaching the previous Creo process.'}}
        }
    }finally{
        if(!$worker.HasExited -and ($Abandon -or !$worker.WaitForExit(500))){$worker.Kill();[void]$worker.WaitForExit(1800)}
        $worker.Dispose()
    }
}

function Close-BwtlComLink($Link,[bool]$RemoveMarker,[bool]$Abandon=$false) {
    Stop-BwtlComLinkWorker $Link $false $Abandon
    if($Link.StopSignal){$Link.StopSignal.Dispose()}
    $Link.Process.Dispose()
    if($RemoveMarker){[IO.File]::Delete($Link.Marker)}
}

function Release-BwtlComTarget($Link) {
    Stop-BwtlComLinkWorker $Link $true
    Write-BwtlComRegistration $Link 'ready'
}

function Connect-BwtlComLink($Link,$Bind=$null) {
    $program=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $workerPath=Join-Path $PSScriptRoot 'Invoke-CreoComResidentLink.ps1'
    $start=[Diagnostics.ProcessStartInfo]::new($program,('-NoProfile -NonInteractive -Sta -ExecutionPolicy Bypass -File "'+$workerPath+'"'))
    $start.WorkingDirectory=$Link.Runtime
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardInput=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    $start.EnvironmentVariables['TEMP']=$Link.Runtime
    $start.EnvironmentVariables['TMP']=$Link.Runtime
    $start.EnvironmentVariables['BWTL_COM_LINK_IDENTITY']=@{Endpoint=$Link.Endpoint;ProcessId=$Link.ProcessId;
        ProcessStart=$Link.ProcessStart;Slot=$Link.Slot;Bind=$Bind}|ConvertTo-Json -Compress
    $Link.Worker=[Diagnostics.Process]::Start($start)
    $errors=$Link.Worker.StandardError.ReadToEndAsync()
    try{
        $ready=Read-BwtlComFrame $Link.Worker.StandardOutput.BaseStream 8500|ConvertFrom-Json
        if($ready.Protocol -cne $bwtlComProtocol -or !$ready.Ok){throw ('PTC resident process setup failed. '+$ready.Message)}
        $id=[string]$ready.Result.ConnectionId
        if(!$id -or ($Link.ConnectionId -and $Link.ConnectionId -cne $id)){throw 'The PTC connection identity changed during handoff.'}
        $Link.ConnectionId=$id
    }catch{
        $failure=$_
        if($Link.Worker.HasExited -and $errors.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion){
            $detail=$errors.GetAwaiter().GetResult().Trim()
            if($detail){$failure=$detail}
        }
        Stop-BwtlComLinkWorker $Link
        throw $failure
    }
}

function Start-BwtlComVendor($First) {
    if(Get-Process pfclscom -ErrorAction SilentlyContinue|Where-Object SessionId -eq $First.Process.SessionId){
        throw 'A separate PTC COM service is already active for this Windows session.'
    }
    $common=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $First.Vendor))
    $start=[Diagnostics.ProcessStartInfo]::new($First.Vendor,'-Embedding')
    $start.WorkingDirectory=$First.Runtime
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $start.EnvironmentVariables['PFCLS_START_DIR']=$First.Runtime
    $start.EnvironmentVariables['PRO_DIRECTORY']=$common
    $start.EnvironmentVariables['PRO_COMM_MSG_EXE']=$First.Communication
    $start.EnvironmentVariables['RPC_TIMEOUT']='5'
    $start.EnvironmentVariables['PATH']=(Join-Path $common 'bin')+';'+$env:PATH
    $start.EnvironmentVariables['TEMP']=$First.Runtime
    $start.EnvironmentVariables['TMP']=$First.Runtime
    [Diagnostics.Process]::Start($start)
}

function Stop-BwtlComVendor($Server) {
    if(!$Server){return}
    try{
        if(!$Server.HasExited){
            $Server.Kill()
            if(!$Server.WaitForExit(2000)){throw 'BWTL could not shut down its own PTC COM process.'}
        }
    }finally{$Server.Dispose()}
}

function Invoke-BwtlComWorker($Link,$Request) {
    Write-BwtlComFrame $Link.Worker.StandardInput.BaseStream ($Request|ConvertTo-Json -Compress) 1500
    Read-BwtlComFrame $Link.Worker.StandardOutput.BaseStream 7000|ConvertFrom-Json
}

function Invoke-BwtlComBroker($First) {
    $links=@{}
    $links.Add($First.Key,$First)
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
    $windowsSession=$First.Process.SessionId
    $pipe=$null
    $server=$null
    $typeLib=$null
    $active=$null
    $hasVendor=$false
    $vendorLock=[Threading.Mutex]::new($false,"Global\BWTL.CreoCom.$($sid.Value)")
    try{
        try{$hasVendor=$vendorLock.WaitOne(18000)}catch [Threading.AbandonedMutexException]{$hasVendor=$true}
        if(!$hasVendor){throw 'The PTC COM service is still being used by another BWTL client.'}
        $env:TEMP=$First.Runtime
        $env:TMP=$First.Runtime
        $env:PRO_COMM_MSG_EXE=$First.Communication
        $env:RPC_TIMEOUT='5'
        Add-Type -Path (Join-Path $PSScriptRoot 'CreoComTypeLibrary.cs')
        $typeLib=[BwtlCreoComTypeLibrary]::new($First.Vendor)
        [void]$typeLib.Acquire()
        $server=Start-BwtlComVendor $First
        $security=[IO.Pipes.PipeSecurity]::new()
        $security.SetAccessRuleProtection($true,$false)
        $security.AddAccessRule([IO.Pipes.PipeAccessRule]::new($sid,[IO.Pipes.PipeAccessRights]::FullControl,
            [Security.AccessControl.AccessControlType]::Allow))
        $pipeName=Get-BwtlComResidentPipeName
        while($links.Count){
            $replaceServer=!$server -or $server.HasExited
            foreach($key in @($links.Keys)){
                $link=$links[$key]
                if($link.Process.HasExited -or ($link.StopSignal -and $link.StopSignal.WaitOne(0))){
                    if($active -and $active.Key -eq $key){$active=$null;$replaceServer=$true}
                    Close-BwtlComLink $link $true $true
                    $links.Remove($key)
                }
            }
            if(!$links.Count){break}
            if($replaceServer){
                if($active){
                    Stop-BwtlComLinkWorker $active $false $true
                    Write-BwtlComRegistration $active 'ready'
                    $active=$null
                }
                $retired=$server
                $server=$null
                Stop-BwtlComVendor $retired
                $server=Start-BwtlComVendor ($links.Values|Select-Object -First 1)
            }
            if(!$pipe){
                $pipe=[IO.Pipes.NamedPipeServerStream]::new($pipeName,[IO.Pipes.PipeDirection]::InOut,1,
                    [IO.Pipes.PipeTransmissionMode]::Byte,[IO.Pipes.PipeOptions]::Asynchronous,65536,65536,$security)
                $listener=$pipe.WaitForConnectionAsync()
            }
            if(!$listener.Wait(150)){
                if($active -and $active.Worker.HasExited){
                    Release-BwtlComTarget $active
                    $active=$null
                }
                $next=$links.Values|Where-Object {!$_.ConnectionId -and $_.Attempts -lt 5 -and $_.RetryAt -le [datetime]::UtcNow}|Select-Object -First 1
                if($next){
                    if($active){
                        Release-BwtlComTarget $active
                        $active=$null
                    }
                    $next.Attempts++
                    try{
                        Connect-BwtlComLink $next
                        $active=$next
                        Write-BwtlComRegistration $next 'connected'
                    }catch{
                        $next.LastError=$_.Exception.GetBaseException().Message
                        $next.RetryAt=[datetime]::UtcNow.AddSeconds([Math]::Min(8,$next.Attempts*2))
                        $state=if($next.Attempts -lt 5){'starting'}else{'unavailable'}
                        Write-BwtlComRegistration $next $state $next.LastError
                    }
                }
                continue
            }
            $disconnectFailed=$false
            try{
                [void]$listener.GetAwaiter().GetResult()
                if(![BwtlCreoComTypeLibrary]::ClientIsInSession($pipe.SafePipeHandle,[uint32]$windowsSession)){
                    throw 'The BWTL COM pipe client is in another Windows session.'
                }
                $request=Read-BwtlComFrame $pipe 2000|ConvertFrom-Json
                if($request.Protocol -cne $bwtlComProtocol){throw 'Invalid BWTL resident COM protocol.'}
                if($request.Operation -ceq 'Register'){
                    $new=New-BwtlComRegistration $request
                    if($new.Vendor -ine $First.Vendor){Close-BwtlComLink $new $false;throw 'The resident PTC service belongs to a different Creo installation.'}
                    if($links.ContainsKey($new.Key)){
                        $same=$links[$new.Key].Profile -ieq $new.Profile
                        Close-BwtlComLink $new $false
                        if(!$same){throw 'The same Creo identity was reported from a different BWTL profile.'}
                    }else{
                        $links.Add($new.Key,$new)
                        Write-BwtlComRegistration $new 'starting'
                    }
                    $result=$true
                }elseif($request.Operation -cin @('Probe','Activate')){
                    $key='{0}.{1}.{2}' -f $request.ProcessId,$request.ProcessStart,$request.Slot
                    $link=$links[$key]
                    if(!$link -or $link.Process.HasExited -or ($link.StopSignal -and $link.StopSignal.WaitOne(0))){
                        throw 'The requested Creo process has no active BWTL COM registration.'
                    }
                    if(!$link.ConnectionId){
                        throw ('The requested Creo COM route is not ready. '+$link.LastError)
                    }
                    if($request.ConnectionId -cne $link.ConnectionId){throw 'The requested Creo identity changed since BWTL verified the COM route.'}
                    $bind=@{ConnectionId=$link.ConnectionId;ExpectedModel=$request.ExpectedModel;
                        ExpectedWorkDir=$request.ExpectedWorkDir;ExpectedWindow=$request.ExpectedWindow}
                    if(!$active -or $active.Key -ne $link.Key -or $active.Worker.HasExited){
                        if($active){
                            if($active.Process.HasExited -or ($active.StopSignal -and $active.StopSignal.WaitOne(0))){
                                $links.Remove($active.Key)
                                Close-BwtlComLink $active $true $true
                                $retired=$server
                                $server=$null
                                Stop-BwtlComVendor $retired
                                $server=Start-BwtlComVendor $link
                            }else{
                                try{Release-BwtlComTarget $active}catch{$disconnectFailed=$true;throw}
                            }
                            $active=$null
                        }
                        Connect-BwtlComLink $link $bind
                        $active=$link
                        Write-BwtlComRegistration $link 'connected'
                    }
                    try{
                        $back=Invoke-BwtlComWorker $link $request
                    }catch{
                        Stop-BwtlComLinkWorker $link $false $true
                        $active=$null
                        Write-BwtlComRegistration $link 'ready'
                        $retired=$server
                        $server=$null
                        Stop-BwtlComVendor $retired
                        $server=Start-BwtlComVendor $link
                        if($request.Operation -cne 'Probe'){throw 'The Creo COM worker timed out. Read native Creo state before retrying.'}
                        Connect-BwtlComLink $link $bind
                        $active=$link
                        Write-BwtlComRegistration $link 'connected'
                        $back=Invoke-BwtlComWorker $link $request
                    }
                    if($back.Protocol -cne $bwtlComProtocol -or !$back.Ok){throw ('PTC resident process reported: '+$back.Message)}
                    $result=$back.Result
                }else{throw 'Unsupported BWTL resident COM operation.'}
                $reply=@{Protocol=$bwtlComProtocol;Ok=$true;Result=$result}
            }catch{
                $reply=@{Protocol=$bwtlComProtocol;Ok=$false;Message=$_.Exception.GetBaseException().Message}
            }
            try{
                Write-BwtlComFrame $pipe ($reply|ConvertTo-Json -Compress) 2000
                # Disconnecting first can discard Windows pipe bytes the client has not read yet.
                $peerClosed=$pipe.ReadAsync([byte[]]::new(1),0,1)
                [void]$peerClosed.Wait(1500)
            }catch{}finally{
                $pipe.Dispose()
                $pipe=$null
            }
            if($disconnectFailed){throw 'BWTL shut down its PTC COM service after an unverified disconnect.'}
        }
    }catch{
        $reason=$_.Exception.GetBaseException().Message
        foreach($link in $links.Values){Write-BwtlComRegistration $link 'unavailable' $reason}
    }finally{
        if($pipe){$pipe.Dispose()}
        foreach($link in $links.Values){Close-BwtlComLink $link $false}
        try{
            Stop-BwtlComVendor $server
        }finally{
            try{if($typeLib){$typeLib.Dispose()}}finally{if($hasVendor){$vendorLock.ReleaseMutex()};$vendorLock.Dispose()}
        }
    }
}

$request=@{Operation='Register';ProcessId=$env:BWTL_COM_RESIDENT_PID;ProcessStart=$env:BWTL_COM_RESIDENT_PROCESS_START;
    ProfileRoot=$env:BWTL_COM_RESIDENT_PROFILE;Slot=$env:BWTL_COM_RESIDENT_SLOT;StopEvent=$env:BWTL_COM_RESIDENT_STOP_EVENT}
$first=New-BwtlComRegistration $request
$election=[Threading.Mutex]::new($false,('Local\'+(Get-BwtlComResidentPipeName)))
$leader=$false
try{
    try{$leader=$election.WaitOne(0)}catch [Threading.AbandonedMutexException]{$leader=$true}
    if($leader){
        Write-BwtlComRegistration $first 'starting'
        Invoke-BwtlComBroker $first
    }else{
        try{[void](Invoke-BwtlComResidentRequest $request 15000 15000)}
        catch{Write-BwtlComRegistration $first 'unavailable' $_.Exception.GetBaseException().Message}
        finally{Close-BwtlComLink $first $false}
    }
}finally{
    if($leader){$election.ReleaseMutex()}
    $election.Dispose()
}
