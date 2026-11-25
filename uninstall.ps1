[CmdletBinding()]
param([string]$InstallRoot,[switch]$Silent,[switch]$PlanOnly,[switch]$DeleteProgramFiles,[switch]$DeleteData)
$ErrorActionPreference='Stop'
if(!$InstallRoot){
    $managedRoot=[Environment]::GetEnvironmentVariable('MWEAI_INSTALL_ROOT','User')
    if(-not $managedRoot){$managedRoot=[Environment]::GetEnvironmentVariable('BWTL_INSTALL_ROOT','User')}
    if(-not $managedRoot){
        $legacyRoot=Join-Path $env:LOCALAPPDATA 'BWTL'
        if(Test-Path -LiteralPath (Join-Path $legacyRoot 'active.json') -PathType Leaf){$managedRoot=$legacyRoot}
    }
    $InstallRoot=if($managedRoot){$managedRoot}else{Join-Path $env:LOCALAPPDATA 'MWEAI'}
}
& (Join-Path $InstallRoot 'Install-BWTL.ps1') -InstallRoot $InstallRoot -Uninstall -Silent:$Silent -PlanOnly:$PlanOnly -DeleteProgramFiles:$DeleteProgramFiles -DeleteData:$DeleteData
