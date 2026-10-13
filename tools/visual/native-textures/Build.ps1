param([Parameter(Mandatory=$true)][string]$ReShadeInclude,
      [string]$OutputDirectory=(Join-Path $PSScriptRoot '..\..\..\artifacts\native-textures'))
$ErrorActionPreference='Stop'
# Run from an x64 MSVC developer shell. Fetch the official ReShade 6.8.0
# source separately; no SDK, DLL, neural model or game texture is bundled here.
$sdk=(Resolve-Path -LiteralPath $ReShadeInclude).Path
if(!(Test-Path -LiteralPath (Join-Path $sdk 'reshade.hpp'))){throw 'ReShade include directory required'}
if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)){throw 'Run from an x64 MSVC developer shell'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$out=(Resolve-Path -LiteralPath $OutputDirectory).Path
Push-Location $out
try {
 & cl.exe /nologo /std:c++17 /EHsc /O2 /MD /utf-8 /LD ('/I'+$sdk) (Join-Path $PSScriptRoot 'NativeTextures.cpp') /Fe:NativeTextures.addon64 /link bcrypt.lib d3d11.lib user32.lib
 if($LASTEXITCODE -ne 0){throw 'Add-on build failed'}
 & cl.exe /nologo /std:c++17 /EHsc /O2 /MD /utf-8 ('/I'+$sdk) (Join-Path $PSScriptRoot 'test_native_textures.cpp') /Fe:test_native_textures.exe /link bcrypt.lib d3d11.lib user32.lib
 if($LASTEXITCODE -ne 0){throw 'Fixture build failed'}
 & .\test_native_textures.exe
 if($LASTEXITCODE -ne 0){throw 'Fixture failed'}
} finally {Pop-Location}
