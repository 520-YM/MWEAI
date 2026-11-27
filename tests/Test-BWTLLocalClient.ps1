#requires -version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory)
$ErrorActionPreference='Stop'
$client=Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\Invoke-BWTL.ps1'
$work=[IO.Path]::GetFullPath($WorkDirectory)
if($work -notmatch '(?i)^E:\\[^\\]' -or (Test-Path -LiteralPath $work)){
    throw 'Choose a fresh E-drive fixture directory.'
}
New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
$env:TEMP=$work
$env:TMP=$work
$code=@'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;

static class MockPeer {
    static readonly Encoding Utf8 = new UTF8Encoding(false, true);

    static byte[] ReadExactly(Stream stream, int size) {
        var value = new byte[size];
        int offset = 0;
        while (offset < size) {
            int n = stream.Read(value, offset, size - offset);
            if (n == 0) throw new EndOfStreamException();
            offset += n;
        }
        return value;
    }

    public static int Main(string[] args) {
        string root = args[0], slot = args[1];
        var process = Process.GetCurrentProcess();
        string start = process.StartTime.ToFileTimeUtc().ToString();
        string identity = "BWTL.Control." + process.Id + "." + start + "." + slot;
        string context = process.Id + ":" + start + ":42";
        string data = Path.Combine(root, "data");
        Directory.CreateDirectory(data);
        File.WriteAllText(Path.Combine(data, "local-control.endpoint"),
            "BWTL/1\n" + identity + "\n" + process.Id + "\n" + start + "\n" + slot + "\n", Utf8);
        while (true) {
            using (var pipe = new NamedPipeServerStream(identity, PipeDirection.InOut, 1,
                                         PipeTransmissionMode.Byte, PipeOptions.None)) {
                pipe.WaitForConnection();
                uint length = BitConverter.ToUInt32(ReadExactly(pipe, 4), 0);
                if (length < 1 || length > 65536) return 3;
                var fields = Utf8.GetString(ReadExactly(pipe,(int)length)).Split('\n');
                if (fields.Length != 4 || fields[0] != "BWTL/1") return 4;
                var output = "";
                int result = 0;
                string action = fields[1], body = fields[3];
                if (action == "STATUS") {
                    output = "pid=" + process.Id + "\nprocessStart=" + start + "\nwindow=2\nmodel=mock_" + slot
                        + "\ntype=PRT\nsubtype=SOLID\nworkDir=" + root + "\nstamp=opaque\ncontext=" + context;
                } else if (action == "READ" && body == "SPLIT") {
                    output = "孔径 Ø15 mm\nnative-read: " + slot;
                } else if (fields[2] != context) {
                    result = -8;
                    output = "stale context";
                } else if (action == "EXEC" && body == "TIMEOUT") {
                    File.AppendAllText(Path.Combine(root, "write.log"), "submitted-once\n", Utf8);
                    Thread.Sleep(1600);
                } else if (action == "EXEC") {
                    output = "verified-write: " + slot + "\n" + body;
                } else if (action == "SAVE" && body == "") {
                    result = -15;
                    output = "save conflict";
                } else {
                    return 5;
                }
                try {
                    byte[] payload = Utf8.GetBytes("BWTL/1\n" + result + "\n" + output);
                    byte[] header = BitConverter.GetBytes(payload.Length);
                    pipe.Write(header, 0, 2);
                    Thread.Sleep(10);
                    pipe.Write(header, 2, 2);
                    pipe.Write(payload, 0, 1);
                    Thread.Sleep(5);
                    pipe.Write(payload, 1, payload.Length - 1);
                    pipe.WaitForPipeDrain();
                } catch (IOException) {}
            }
        }
    }
}
'@
$stub=Join-Path $work 'MockPeer.cs'
$exe=Join-Path $work 'xtop.exe'
[IO.File]::WriteAllText($stub,$code,[Text.UTF8Encoding]::new($true))
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $csc /nologo /target:exe ("/out:"+$exe) $stub
if($LASTEXITCODE){throw 'Unable to build the private pipe fixture.'}
function Assert([bool]$Ok,[string]$TestName){
    if(!$Ok){throw "$TestName failed"}
    "$TestName=1"
}

$one=Join-Path $work 'profile1'
$two=Join-Path $work 'profile2'
$roots=@($one,$two)
$peers=@()
try {
    $peers+=Start-Process -FilePath $exe -ArgumentList ('"'+$one+'" 1') -WorkingDirectory $work -PassThru -WindowStyle Hidden
    $peers+=Start-Process -FilePath $exe -ArgumentList ('"'+$two+'" 2') -WorkingDirectory $work -PassThru -WindowStyle Hidden
    $deadline=[DateTime]::UtcNow.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 60
        $list=(& $client -Action List -ProfileRoot $roots -Json) | ConvertFrom-Json
    } while($list.Count -ne 2 -and [DateTime]::UtcNow -lt $deadline)
    Assert ($list.Count -eq 2 -and $list[0].Session -eq 1 -and $list[1].Session -eq 2) 'TWO_ACTIVE_CAD_SESSION_IDS'

    $status1=(& $client -Action Status -ProfileRoot $roots -Session 1 -Json) | ConvertFrom-Json
    $status2=(& $client -Action Status -ProfileRoot $roots -Session 2 -Json) | ConvertFrom-Json
    Assert ($status1.Data.model -eq 'mock_1' -and $status2.Data.model -eq 'mock_2' -and
            $status1.ProcessId -ne $status2.ProcessId) 'STATUS_FRAMED_AND_SELECTED'
    $read=(& $client -Action Read -ProfileRoot $roots -Session 2 -Command SPLIT -Json) | ConvertFrom-Json
    Assert ($read.Body -eq "孔径 Ø15 mm$([char]10)native-read: 2") 'UTF8_SPLIT_RESPONSE'

    & $client -Action Execute -ProfileRoot $roots -Session 2 -ExpectedContext $status1.Data.context -Command 'inapplicable' -Json
    Assert ($LASTEXITCODE -eq 2) 'CROSS_PROCESS_TOKEN_REJECTED'
    $write=(& $client -Action Execute -ProfileRoot $roots -Session 1 -ExpectedContext $status1.Data.context -Command 'one native operation' -Json) |
        ConvertFrom-Json
    Assert ($write.Code -eq 0 -and $write.Body -eq "verified-write: 1$([char]10)one native operation") 'BOUND_WRITE'
    $save=(& $client -Action Save -ProfileRoot $roots -Session 2 -ExpectedContext $status2.Data.context -Json) | ConvertFrom-Json
    Assert ($LASTEXITCODE -eq 1 -and $save.Code -eq -15 -and $save.Body -eq 'save conflict') 'NATIVE_CONFLICT_RETURNS_FAILURE'

    & $client -Action Execute -ProfileRoot $roots -Session 1 -ExpectedContext $status1.Data.context -Command 'TIMEOUT' -TimeoutMs 950 -Json
    Assert ($LASTEXITCODE -eq 2 -and @(Get-Content -LiteralPath (Join-Path $one 'write.log')).Count -eq 1) 'WRITE_TIMEOUT_NO_RETRY'
    Stop-Process -Id $peers[0].Id
    $peers[0].WaitForExit(3000) | Out-Null
    $remaining=(& $client -Action List -ProfileRoot $roots -Json) | ConvertFrom-Json
    Assert (@($remaining).Count -eq 1 -and $remaining.Session -eq 2) 'STALE_DESCRIPTOR_NOT_ROUTABLE'
    'BWTL_LOCAL_CLIENT_PASS=1'
} finally {
    foreach($peer in $peers){
        if(!$peer.HasExited){Stop-Process -Id $peer.Id}
        $peer.Dispose()
    }
}
