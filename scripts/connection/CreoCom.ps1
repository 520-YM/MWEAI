. (Join-Path $PSScriptRoot 'CreoComResidentCommon.ps1')

function Invoke-BwtlCreoCom {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Endpoint,
        [Parameter(Mandatory)] [ValidateSet('Probe','Activate')] [string] $Operation,
        [Parameter(Mandatory)] [string] $ConnectionId,
        [Parameter(Mandatory)] [string] $ExpectedWorkDir,
        [Parameter(Mandatory)] [AllowEmptyString()] [string] $ExpectedModel,
        [Nullable[int]] $ExpectedWindow,
        [string] $TargetModel,
        [string] $TargetPath,
        [string] $Lease,
        [switch] $RequireResident,
        [ValidateRange(1,30)] [int] $TimeoutSeconds = 6
    )

    if (!$ConnectionId -or $ConnectionId.Length -gt 1024 -or $ConnectionId -match '[\x00-\x1f\x7f]') {
        throw 'Invalid Creo VB API connection ID.'
    }
    $target = Get-Process -Id ([int]$Endpoint.ProcessId) -ErrorAction Stop
    if ($target.ProcessName -ine 'xtop' -or $target.StartTime.ToFileTimeUtc() -ne [long]$Endpoint.ProcessStart -or
        $target.SessionId -ne (Get-Process -Id $PID).SessionId) { throw 'The selected Creo process changed.' }
    $objectDirectory = Split-Path -Parent $target.MainModule.FileName
    if ($objectDirectory -notmatch '(?i)\\Common Files\\x86e_win64\\obj$') { throw 'The Creo 64-bit VB API installation path is unavailable.' }
    $commonDirectory = Split-Path -Parent (Split-Path -Parent $objectDirectory)
    $vendor = Join-Path $objectDirectory 'pfclscom.exe'
    $communication = Join-Path $objectDirectory 'pro_comm_msg.exe'
    if (!(Test-Path -LiteralPath $vendor -PathType Leaf) -or !(Test-Path -LiteralPath $communication -PathType Leaf)) {
        throw 'The selected Creo installation lacks the vendor VB API runtime.'
    }

    $profile = [IO.Path]::GetFullPath([string]$Endpoint.ProfileRoot)
    $workingDirectory = [IO.Path]::GetFullPath($ExpectedWorkDir).TrimEnd('\')
    if ($profile -notmatch '(?i)^E:\\[^\\]' -or $workingDirectory -notmatch '(?i)^E:\\[^\\]') {
        throw 'The BWTL COM runtime and Creo working directory must be on E:.'
    }
    if ($Operation -eq 'Activate' -and $TargetModel -notmatch '(?i)^[^\\/:\x00-\x20]+\.(prt|asm)$') {
        throw 'An attested typed Creo model name is required.'
    }
    if ($TargetPath) {
        $TargetPath = [IO.Path]::GetFullPath($TargetPath)
        if ($TargetPath -notmatch '(?i)^E:\\[^\\]') { throw 'The exact target must be on the E drive.' }
    }

    if ($RequireResident) {
        $runtime=Get-BwtlComRuntime $profile
        $marker=Join-Path $runtime ('resident-{0}-{1}.json' -f $Endpoint.ProcessId,$Endpoint.ProcessStart)
        $clock=[Diagnostics.Stopwatch]::StartNew()
        do {
            $state=$null
            if (Test-Path -LiteralPath $marker -PathType Leaf) {
                if ((Get-Item -LiteralPath $marker).Length -gt 8192) { throw 'The resident COM status file is invalid.' }
                $state=[IO.File]::ReadAllText($marker,$bwtlComEncoding)|ConvertFrom-Json
                if ($state.Pid -ne $Endpoint.ProcessId -or $state.ProcessStart -cne $Endpoint.ProcessStart -or
                    $state.Slot -ne $Endpoint.Session) { throw 'The resident COM status targets a different Creo process.' }
                if ($state.State -ceq 'unavailable') { throw ('The resident Creo VB API could not attach. '+$state.Message) }
                if ($state.State -cin @('connected','ready')) { break }
            }
            Start-Sleep -Milliseconds 120
        } while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds)
        if (!$state -or $state.State -cnotin @('connected','ready')) { throw 'The Creo VB API connection is starting. Read its status before retrying.' }
        return Invoke-BwtlComResidentRequest @{
            Operation=$Operation;ProcessId=$Endpoint.ProcessId;ProcessStart=$Endpoint.ProcessStart;Slot=$Endpoint.Session;
            ConnectionId=$ConnectionId;ExpectedModel=$ExpectedModel;ExpectedWorkDir=$workingDirectory;ExpectedWindow=$ExpectedWindow;
            TargetModel=$TargetModel;TargetPath=$TargetPath;Lease=$Lease
        } 22000
    }

    $runtime = Join-Path $profile 'data\connection-runtime'
    for ($directory = $runtime; $directory; $directory = Split-Path -Parent $directory) {
        if ((Test-Path -LiteralPath $directory) -and
            ((Get-Item -LiteralPath $directory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'The BWTL COM runtime directory traverses a reparse point.'
        }
    }
    [void][IO.Directory]::CreateDirectory($runtime)
    $registeredType = $null
    $server = $null
    $worker = $null
    $ownsLock = $false
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $mutex = [Threading.Mutex]::new($false, "Global\BWTL.CreoCom.$identity")
    try {
        try { $ownsLock = $mutex.WaitOne(2000) }
        catch [Threading.AbandonedMutexException] { $ownsLock = $true }
        if (!$ownsLock) { throw 'Another BWTL COM action is still running. Wait for its readback before retrying.' }
        if (Get-Process pfclscom -ErrorAction SilentlyContinue | Where-Object SessionId -eq $target.SessionId) {
            throw 'A Creo VB API server is already active in this Windows session.'
        }
        if (!('BwtlCreoComTypeLibrary' -as [type])) {
            $oldTemp = $env:TEMP
            $oldTmp = $env:TMP
            try {
                $env:TEMP = $runtime
                $env:TMP = $runtime
                Add-Type -Path (Join-Path $PSScriptRoot 'CreoComTypeLibrary.cs')
            } finally {
                $env:TEMP = $oldTemp
                $env:TMP = $oldTmp
            }
        }
        $registeredType = [BwtlCreoComTypeLibrary]::new($vendor)
        [void]$registeredType.Acquire()

        $serverStart = [Diagnostics.ProcessStartInfo]::new($vendor,'-Embedding')
        $serverStart.WorkingDirectory = $runtime
        $serverStart.UseShellExecute = $false
        $serverStart.CreateNoWindow = $true
        $serverStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $serverStart.EnvironmentVariables['PFCLS_START_DIR'] = $runtime
        $serverStart.EnvironmentVariables['PRO_DIRECTORY'] = $commonDirectory
        $serverStart.EnvironmentVariables['PRO_COMM_MSG_EXE'] = $communication
        $serverStart.EnvironmentVariables['RPC_TIMEOUT'] = [string]$TimeoutSeconds
        $serverStart.EnvironmentVariables['PATH'] = (Join-Path $commonDirectory 'bin') + ';' + $env:PATH
        $serverStart.EnvironmentVariables['TEMP'] = $runtime
        $serverStart.EnvironmentVariables['TMP'] = $runtime
        $server = [Diagnostics.Process]::Start($serverStart)

        $workerPath = Join-Path $PSScriptRoot 'Invoke-CreoComWorker.ps1'
        $workerStart = [Diagnostics.ProcessStartInfo]::new((Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'))
        $workerStart.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $workerPath + '"'
        $workerStart.WorkingDirectory = $runtime
        $workerStart.UseShellExecute = $false
        $workerStart.CreateNoWindow = $true
        $workerStart.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $workerStart.RedirectStandardOutput = $true
        $workerStart.RedirectStandardError = $true
        $workerStart.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
        $workerStart.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
        $workerStart.EnvironmentVariables['TEMP'] = $runtime
        $workerStart.EnvironmentVariables['TMP'] = $runtime
        $workerStart.EnvironmentVariables['PRO_COMM_MSG_EXE'] = $communication
        $workerStart.EnvironmentVariables['BWTL_CREO_COM_REQUEST'] = [pscustomobject]@{
            Operation = $Operation; ConnectionId = $ConnectionId; ExpectedModel = $ExpectedModel
            ExpectedWorkDir = $workingDirectory; ExpectedWindow = $ExpectedWindow
            TargetModel = $TargetModel; TargetPath = $TargetPath; TimeoutSeconds = $TimeoutSeconds
        } | ConvertTo-Json -Compress
        $worker = [Diagnostics.Process]::Start($workerStart)
        $stdout = $worker.StandardOutput.ReadToEndAsync()
        $stderr = $worker.StandardError.ReadToEndAsync()
        if (!$worker.WaitForExit(($TimeoutSeconds + 6) * 1000)) { throw 'The vendor VB API worker timed out. Read native Creo state before retrying.' }
        $report = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult().Trim()
        if ($worker.ExitCode -ne 0 -or $errors) { throw "Creo VB API reported $errors" }
        if ($target.HasExited) { throw 'The selected Creo process exited before COM readback.' }
        $result = $report | ConvertFrom-Json -ErrorAction Stop
        if ($null -eq $result -or $null -eq $result.Window -or $result.WorkDir -ine $workingDirectory) {
            throw 'The vendor VB API returned an invalid state.'
        }
        $result|Add-Member -NotePropertyName Mode -NotePropertyValue 'OnDemand'
        return $result
    } finally {
        try {
            if ($null -ne $worker) {
                try { if (!$worker.HasExited) { $worker.Kill(); [void]$worker.WaitForExit(2000) } }
                finally { $worker.Dispose() }
            }
            if ($null -ne $server) {
                try { if (!$server.WaitForExit(500)) { $server.Kill(); [void]$server.WaitForExit(2000) } }
                finally { $server.Dispose() }
            }
        } finally {
            try { if ($null -ne $registeredType) { $registeredType.Dispose() } }
            finally { if ($ownsLock) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
        }
    }
}
