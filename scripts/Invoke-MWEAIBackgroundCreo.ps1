[CmdletBinding()]
param([Parameter(Mandatory)][string]$JobDirectory,[switch]$OpenResult)
$ErrorActionPreference='Stop'
$job=[IO.Path]::GetFullPath($JobDirectory)
if($job -notmatch '^E:\\project\\MWEAI-49-[a-f0-9]{32}$' -or !(Test-Path -LiteralPath $job -PathType Container)){
    throw 'Invalid MWEAI E-drive job directory.'
}
function Assert-PlainPath([string]$Path){
    for($p=$Path;$p;$p=Split-Path -Parent $p){
        if((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Unsupported reparse point: $p"}
    }
}
Assert-PlainPath $job
$creo=Join-Path $env:ProgramFiles 'PTC\Creo 10.0.0.0\Parametric\bin\parametric.exe'
$package=Split-Path -Parent $PSScriptRoot
$dll=Join-Path $package 'bin\MWEAI-BackgroundCreo.dll'
$models=Join-Path $job 'Models'
$status=Join-Path $job 'status.txt'
$result=Join-Path $job 'result.txt'
$utf8=[Text.UTF8Encoding]::new($false)
$env:TEMP=$job
$env:TMP=$job
function Write-State([string]$State){
    $tmp="$status.new"
    [IO.File]::WriteAllText($tmp,$State,[Text.Encoding]::Unicode)
    if([IO.File]::Exists($status)){[IO.File]::Replace($tmp,$status,"$status.previous")}
    else{[IO.File]::Move($tmp,$status)}
}
function Start-IsolatedCreo([string]$Phase,[string]$Workspace,[string]$OpenFile){
    if(!(Test-Path -LiteralPath $creo -PathType Leaf)){throw 'Creo 10 installation was not found.'}
    $run=Join-Path $job $Phase
    foreach($dir in 'home','alternate','cache','temp'){New-Item -ItemType Directory -Path (Join-Path $run $dir) -Force | Out-Null}
    $config=@("trail_dir $run",'prompt_on_exit no','enable_legacy_datum_planes yes')
    if(!$OpenFile){
        if(!(Test-Path -LiteralPath $dll -PathType Leaf)){throw 'Packaged MWEAI background Toolkit is missing.'}
        $registry=Join-Path $run 'worker.dat'
        @("NAME MweaiBackgroundFloorstand",'STARTUP dll',"EXEC_FILE $dll","TEXT_DIR $package\resources",'DELAY_START FALSE','END') |
            Set-Content -LiteralPath $registry -Encoding ascii
        $config+=@("protkdat $registry",'open_protk_unsigned_apps always')
    }
    $config | Set-Content -LiteralPath "$run\home\config.pro" -Encoding ascii
    Copy-Item -LiteralPath "$run\home\config.pro" -Destination "$run\alternate\config.pro"
    $procInfo=[Diagnostics.ProcessStartInfo]::new($creo)
    $procInfo.WorkingDirectory=$Workspace
    $procInfo.UseShellExecute=$false
    if(!$OpenFile){
        $procInfo.Arguments='-g:no_graphics -i:rpc_input'
        $procInfo.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    }else{
        $procInfo.Arguments='"'+$OpenFile+'"'
    }
    foreach($name in @('HOME','PTC_CREO_ALT_SETTINGS_PATH','PTC_WF_ROOT','TEMP','TMP')){
        $folder=@{HOME='home';PTC_CREO_ALT_SETTINGS_PATH='alternate';PTC_WF_ROOT='cache';TEMP='temp';TMP='temp'}[$name]
        $procInfo.EnvironmentVariables[$name]=Join-Path $run $folder
    }
    foreach($name in 'PROTKDAT','MWEAI_BACKGROUND_JOB','MWEAI_BACKGROUND_VERIFY','BWTL_INSTALL_ROOT','MWEAI_INSTALL_ROOT','BWTL_INSTANCE_NUMBER'){
        $procInfo.EnvironmentVariables.Remove($name)
    }
    $procInfo.EnvironmentVariables['BWTL_PROFILE_ROOT']=$run
    $procInfo.EnvironmentVariables['BWTL_ISOLATED_SESSION']='1'
    $procInfo.EnvironmentVariables['PRO_LANG']='eng'
    if(!$OpenFile){$procInfo.EnvironmentVariables['MWEAI_BACKGROUND_JOB']='yes'}
    if($Phase -eq 'verify'){$procInfo.EnvironmentVariables['MWEAI_BACKGROUND_VERIFY']='yes'}
    $process=[Diagnostics.Process]::Start($procInfo)
    [IO.File]::WriteAllText((Join-Path $run 'launcher-pid.txt'),[string]$process.Id,$utf8)
    if($OpenFile){return}
    $log=Join-Path $Workspace $(if($Phase -eq 'verify'){'verify.log'}else{'build-run.log'})
    $deadline=[DateTime]::UtcNow.AddMinutes(8)
    do{
        if(Test-Path -LiteralPath $log){
            $final=Select-String -LiteralPath $log -Pattern '^FINAL=(-?[0-9]+)$' | Select-Object -Last 1
            if($final){
                if($final.Matches[0].Groups[1].Value -ne '0'){throw "$Phase rejected by Creo; inspect $log"}
                return
            }
        }
        if($process.HasExited){throw "$Phase Creo launcher exited before a native result was written."}
        Start-Sleep -Milliseconds 500
    }while([DateTime]::UtcNow -lt $deadline)
    throw "$Phase timed out. Inspect the task logs and launcher PID under $run."
}
if($OpenResult){
    if(!(Test-Path -LiteralPath $result -PathType Leaf) -or [IO.File]::ReadAllText($status).Trim() -ne 'done'){throw 'The job has no verified result.'}
    $target=[IO.Path]::GetFullPath([IO.File]::ReadAllText($result).Trim())
    if((Split-Path -Parent $target) -ine $models -or (Split-Path -Leaf $target) -notmatch '^mw49_floorstand_narrow\.asm\.[0-9]+$'){throw 'Invalid assembly result path'}
    Start-IsolatedCreo ('review-'+[guid]::NewGuid().ToString('N').Substring(0,8)) $models $target
    exit 0
}
try{
    Write-State 'copy'
    $request=Get-Content -LiteralPath (Join-Path $job 'request.txt') -Encoding Unicode
    if($request.Count -ne 2){throw 'Expected source assembly and 49-inch reference.'}
    $source=[IO.Path]::GetFullPath($request[0])
    $reference=[IO.Path]::GetFullPath($request[1])
    foreach($path in $source,$reference){
        if($path -notmatch '^E:\\' -or !(Test-Path -LiteralPath $path -PathType Leaf)){throw 'Both saved source models must reside on E:.'}
        Assert-PlainPath $path
    }
    if((Split-Path -Leaf $source) -notmatch '^m26g2000000\.asm\.[0-9]+$' -or (Split-Path -Leaf $reference) -notmatch '^49inch\.prt\.[0-9]+$'){throw 'This recipe requires M26G2000000 and a 49INCH reference.'}
    $parent=Split-Path -Parent $source
    $candidates=Get-ChildItem -LiteralPath $parent -File | Where-Object {
        $_.Name -match '\.(asm|prt)\.[0-9]+$' -and $_.Name -notmatch '^(49inch\.prt\.|m26g2000000\.asm\.|regen_backup_model)' -and !($_.Attributes -band [IO.FileAttributes]::ReparsePoint)
    }
    New-Item -ItemType Directory -Path $models | Out-Null
    $candidates | Group-Object {$_.Name -replace '\.[0-9]+$',''} | ForEach-Object {
        $entry=$_.Group | Sort-Object {[long]($_.Extension.TrimStart('.'))} -Descending | Select-Object -First 1
        Copy-Item -LiteralPath $entry.FullName -Destination $models
    }
    Copy-Item -LiteralPath $source,$reference -Destination $models
    Write-State 'build'
    Start-IsolatedCreo 'build' $models $null
    $target=Get-ChildItem -LiteralPath $models -File -Filter 'mw49_floorstand_narrow.asm.*' |
        Where-Object Name -Match '\.asm\.[0-9]+$' |
        Sort-Object {[long]($_.Extension.TrimStart('.'))} -Descending | Select-Object -First 1
    if(!$target){throw 'Creo did not save the derived top assembly.'}
    $verified=Join-Path $job 'Verification'
    New-Item -ItemType Directory -Path $verified | Out-Null
    Get-ChildItem -LiteralPath $models -File | Where-Object Name -match '\.(asm|prt)\.[0-9]+$' |
        Copy-Item -Destination $verified
    Write-State 'verify'
    Start-IsolatedCreo 'verify' $verified $null
    [IO.File]::WriteAllText($result,$target.FullName,[Text.Encoding]::Unicode)
    Write-State 'done'
}catch{
    [IO.File]::WriteAllText((Join-Path $job 'error.txt'),$_.Exception.Message,[Text.Encoding]::Unicode)
    Write-State 'failed'
    exit 1
}


