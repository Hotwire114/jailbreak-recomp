param([string]$CompilerRoot,[string]$BuildDirectory)
$ErrorActionPreference='Stop'
if(-not $CompilerRoot){$CompilerRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\work\tooling\toolchain\bin'))}
$env:PATH=$CompilerRoot+';'+$env:PATH
$source=Join-Path $PSScriptRoot 'source'
if(-not $BuildDirectory){$BuildDirectory=Join-Path $PSScriptRoot 'build'}
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$BuildDirectory=[IO.Path]::GetFullPath($BuildDirectory)
$object=Join-Path $BuildDirectory 'launcher-icon.o'
$oldTemp=$env:TEMP;$oldTmp=$env:TMP
$env:TEMP=$BuildDirectory;$env:TMP=$BuildDirectory
Push-Location $source
try{
 & (Join-Path $CompilerRoot 'llvm-windres.exe') launcher.rc -O coff -o $object
 if($LASTEXITCODE -ne 0){throw 'Icon compilation failed.'}
 & (Join-Path $CompilerRoot 'clang.exe') -std=c11 -O2 -Wall -Wextra -municode -mwindows -static '-Wl,--stack,8388608' -Irecomp_net_auth/include launcher.c owner_assets.c recomp_net_auth/auth/rnet_auth.c recomp_net_auth/auth/rnet_open_url.c recomp_net_auth/auth/rnet_sha256.c $object -o (Join-Path $PSScriptRoot 'RoadRashJailbreak.exe') -luser32 -lgdi32 -lcomdlg32 -lbcrypt -lshell32 -lshlwapi -lole32 -loleaut32 -lwindowscodecs -luuid -lmsimg32 -lws2_32 -lwinhttp
 if($LASTEXITCODE -ne 0){throw 'Launcher build failed.'}
}finally{Pop-Location;$env:TEMP=$oldTemp;$env:TMP=$oldTmp;Remove-Item -LiteralPath $object -ErrorAction SilentlyContinue}
