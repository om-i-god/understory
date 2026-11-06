# Make an MSVC import library (obs.lib) from an installed OBS's obs.dll, so the plugin can link against it.
# Run from a "x64 Native Tools" / Developer PowerShell (needs dumpbin and lib on PATH).
#   tools/make-obs-lib.ps1 -Dll "C:\Program Files\obs-studio\bin\64bit\obs.dll" -Out deps\obs.lib
param(
  [Parameter(Mandatory = $true)][string]$Dll,
  [Parameter(Mandatory = $true)][string]$Out,
  [string]$Machine = "x64"
)
$ErrorActionPreference = "Stop"
$def = [System.IO.Path]::ChangeExtension($Out, ".def")
$names = & dumpbin /nologo /exports $Dll | ForEach-Object {
  if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $Matches[1] }
}
if (-not $names -or $names.Count -lt 100) { throw "found only $($names.Count) exports in $Dll" }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
@("LIBRARY obs", "EXPORTS") + $names | Set-Content -Encoding ascii $def
& lib /nologo /def:$def /machine:$Machine /out:$Out
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed" }
Write-Host "wrote $Out ($($names.Count) exports)"
