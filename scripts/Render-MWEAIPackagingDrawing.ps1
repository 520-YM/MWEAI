param([string]$InputPdf,[string]$OutputPng,[uint32]$PageIndex=0)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime] | Out-Null
[Windows.Data.Pdf.PdfDocument,Windows.Data.Pdf,ContentType=WindowsRuntime] | Out-Null
[Windows.Data.Pdf.PdfPageRenderOptions,Windows.Data.Pdf,ContentType=WindowsRuntime] | Out-Null
[Windows.Storage.Streams.InMemoryRandomAccessStream,Windows.Storage.Streams,ContentType=WindowsRuntime] | Out-Null
$method=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.IsGenericType -and $_.GetParameters()[0].ParameterType.Name.StartsWith('IAsyncOperation')
} | Select-Object -First 1
function Read-Result($operation,[Type]$type){
    $task=$method.MakeGenericMethod($type).Invoke($null,@($operation))
    $task.GetAwaiter().GetResult()
}
$file=Read-Result ([Windows.Storage.StorageFile]::GetFileFromPathAsync($InputPdf)) ([Windows.Storage.StorageFile])
$pdf=Read-Result ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
$page=$pdf.GetPage($PageIndex)
$stream=[Windows.Storage.Streams.InMemoryRandomAccessStream]::new()
try {
    $opts=[Windows.Data.Pdf.PdfPageRenderOptions]::new()
    $opts.DestinationWidth=1400
    $asAction=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction'
    } | Select-Object -First 1
    $asAction.Invoke($null,@($page.RenderToStreamAsync($stream,$opts))).GetAwaiter().GetResult()
    $stream.Seek(0)
    $managed=[System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream)
    $out=[IO.File]::Open($OutputPng,[IO.FileMode]::CreateNew)
    try {$managed.CopyTo($out)} finally {$out.Dispose();$managed.Dispose()}
} finally {$page.Dispose();$stream.Dispose()}
