[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Pdf,
    [Parameter(Mandatory=$true)][string]$Exchange,
    [Parameter(Mandatory=$true)][string]$Result
)
$ErrorActionPreference='Stop'
$utf8=[Text.UTF8Encoding]::new($true)
$Exchange=[IO.Path]::GetFullPath($Exchange)
$Pdf=[IO.Path]::GetFullPath($Pdf)
$Result=[IO.Path]::GetFullPath($Result)
$dataDirectory=[IO.Path]::GetDirectoryName($Exchange.TrimEnd('\','/'))+'\'
if([IO.Path]::GetPathRoot($Exchange) -ine 'E:\' -or [IO.Path]::GetPathRoot($Pdf) -ine 'E:\' -or
   [IO.Path]::GetDirectoryName($Result) -ine $Exchange -or
   [IO.Path]::GetFileName($Result) -notmatch '^pressfit-pdf-\d+-\d+\.tsv$' -or
   !$Pdf.StartsWith($dataDirectory,[StringComparison]::OrdinalIgnoreCase) -or
   [IO.Path]::GetFileName([IO.Path]::GetDirectoryName($Pdf)) -ine 'attachments' -or
   [IO.Path]::GetFileName($Pdf) -notmatch '^file-[\d-]+\.pdf$' -or
   ([IO.File]::GetAttributes($Pdf) -band [IO.FileAttributes]::ReparsePoint)){
    throw 'Invalid BWTL attachment or result path'
}
$env:TEMP=$Exchange
$env:TMP=$Exchange
. (Join-Path $PSScriptRoot 'Read-BWTLChatAttachment.ps1')

# Collect OCR words into physical rows. OCR can split distant table cells
# into different text lines, so join only words with overlapping Y coordinates.
function Get-HoleOcrRows($Scanned){
    $clusters=[Collections.Generic.List[object]]::new()
    $words=@(foreach($item in $Scanned.Lines){
        foreach($word in $item.Words){
            $rect=$word.BoundingRect
            [pscustomobject]@{Text=$word.Text;X=$rect.X;Y=$rect.Y+$rect.Height/2;Height=$rect.Height}
        }
    })
    $wordOrder=@{Expression={if($_.Text -match '^[.．。]$'){1}else{0}}},'Y','X'
    foreach($word in ($words | Sort-Object -Property $wordOrder)){
        $punctuation=$word.Text -match '^[.．。]$'
        $row=$null
        $delta=[double]::PositiveInfinity
        foreach($candidate in $clusters){
            $distance=[Math]::Abs($candidate.Y-$word.Y)
            $tolerance=if($punctuation){0.8*$candidate.Height}else{0.45*[Math]::Min($candidate.Height,$word.Height)}
            if($distance -le $tolerance -and $distance -lt $delta){
                $delta=$distance
                $row=$candidate
            }
        }
        if(!$row){
            if($punctuation){continue}
            $row=[pscustomobject]@{Y=$word.Y;Height=$word.Height;Words=[Collections.Generic.List[object]]::new()}
            $clusters.Add($row)
        }
        $row.Words.Add($word)
    }
    foreach($row in ($clusters | Sort-Object Y)){
        ($row.Words | Sort-Object X | ForEach-Object Text) -join ' '
    }
}

$lines=[Collections.Generic.List[string]]::new()
$lines.Add('BWTL-PRESSFIT-PDF-1')
$images=@()
try{
    $pages=@(Read-BwtlPdf $Pdf 1 $Exchange 1 4)
    $images=@($pages|Where-Object type -eq 'localImage'|ForEach-Object path)
    if(!$images.Count){throw 'NO_PAGES'}
    if(@($pages|Where-Object {$_.type -eq 'text' -and $_.text -match '^PDF 附件 1 本轮只提供'}).Count){
        throw 'PAGE_LIMIT'
    }
    $storageType=[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime]
    $accessType=[Windows.Storage.FileAccessMode,Windows.Storage,ContentType=WindowsRuntime]
    $streamType=[Windows.Storage.Streams.IRandomAccessStream,Windows.Storage.Streams,ContentType=WindowsRuntime]
    $decoderType=[Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime]
    $bitmapType=[Windows.Graphics.Imaging.SoftwareBitmap,Windows.Graphics.Imaging,ContentType=WindowsRuntime]
    $ocrType=[Windows.Media.Ocr.OcrEngine,Windows.Foundation,ContentType=WindowsRuntime]
    $resultType=[Windows.Media.Ocr.OcrResult,Windows.Foundation,ContentType=WindowsRuntime]
    $ocr=$ocrType::TryCreateFromUserProfileLanguages()
    if(!$ocr){throw 'OCR_UNAVAILABLE'}
    $header=$false
    $dataRows=0
    $hasZ=$true
    $number='[+-]?\d{1,7}(?:\.\d{1,6})?'
    $spec='[A-Z0-9][A-Z0-9 ._\-×:]{0,62}'
    $group='[\p{L}_][\p{L}\p{Nd}_\-]{0,22}'
    for($i=0;$i -lt $images.Count;$i++){
        $file=Wait-BwtlWinRt ($storageType::GetFileFromPathAsync($images[$i])) $storageType
        $stream=Wait-BwtlWinRt ($file.OpenAsync($accessType::Read)) $streamType
        try{
            $decoder=Wait-BwtlWinRt ($decoderType::CreateAsync($stream)) $decoderType
            $bitmap=Wait-BwtlWinRt ($decoder.GetSoftwareBitmapAsync()) $bitmapType
            try{$recognized=Wait-BwtlWinRt ($ocr.RecognizeAsync($bitmap)) $resultType}
            finally{$bitmap.Dispose()}
        }finally{$stream.Dispose()}
        $page=$i+1
        $row=0
        foreach($raw in (Get-HoleOcrRows $recognized)){
            $row++
            $value=$raw.Normalize([Text.NormalizationForm]::FormKC).Trim().Replace('−','-').Replace('Ø','').Replace('Φ','').Replace('φ','')
            $value=$value -replace '(?<=\d)\s*[.。]\s*(?=\d)', '.' -replace '(?<!\d)([+\-])\s+(?=\d)', '$1'
            if($value -match '^(?:PANEL|板件ID|组件ID)\s+X(?:\(MM\))?\s+Y(?:\(MM\))?(?<z>\s+Z(?:\(MM\))?)?\s+(?:D|DIA|孔径)(?:\(MM\))?\s+(?:GROUP|分组|组名)\s+(?:SPEC|规格|型号)$'){
                $header=$true
                $hasZ=[bool]$Matches['z']
                continue
            }
            if(!$header -or $value -notmatch '^(?:ID\s*:\s*)?\d'){continue}
            $columns="^(?:ID\s*:\s*)?(?<id>\d{1,9})\s+(?<x>$number)\s+(?<y>$number)"
            if($hasZ){$columns+="\s+(?<z>$number)"}
            $columns+="\s+(?<d>$number)\s+(?<group>$group)\s+(?<spec>$spec)$"
            if($value -notmatch $columns){
                $lines.Add("BAD"+[char]9+$page+[char]9+$row)
                continue
            }
            $z=if($hasZ){$Matches.z}else{'-'}
            $fields=@('ROW',$page,$row,$Matches.id,$Matches.x,$Matches.y,$z,$Matches.d,$Matches.group,$Matches.spec)
            $lines.Add(($fields -join [char]9))
            $dataRows++
            if($dataRows -gt 128){throw 'ROW_LIMIT'}
        }
    }
    if(!$header){throw 'NO_HEADER'}
    if(!$dataRows -and $lines.Count -eq 1){throw 'NO_ROWS'}
}catch{
    $lines.Clear()
    $lines.Add('BWTL-PRESSFIT-PDF-1')
    $reason=switch -Regex ($_.Exception.Message){
        '^NO_PAGES$'{'NO_PAGES'} '^PAGE_LIMIT$'{'PAGE_LIMIT'} '^OCR_UNAVAILABLE$'{'OCR_UNAVAILABLE'}
        '^ROW_LIMIT$'{'ROW_LIMIT'} '^NO_HEADER$'{'NO_HEADER'} '^NO_ROWS$'{'NO_ROWS'} default{'PDF_OR_OCR_FAILED'}
    }
    $lines.Add("ERROR"+[char]9+$reason)
}finally{
    foreach($image in $images){
        if([IO.File]::Exists($image)){[IO.File]::Delete($image)}
    }
}
$stream=[IO.File]::Open($Result,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
try{
    $writer=[IO.StreamWriter]::new($stream,$utf8)
    try{foreach($line in $lines){$writer.WriteLine($line)}}finally{$writer.Dispose()}
}finally{$stream.Dispose()}