# Install Understory for OBS on Windows (run in PowerShell from the unzipped release folder, or after a build).
# Copies understory/ into C:\ProgramData\obs-studio\plugins\ (needs an administrator PowerShell). Restart OBS afterwards.
param([string]$From = "")
$ErrorActionPreference = "Stop"
if (-not $From) {
  $here = Split-Path -Parent $MyInvocation.MyCommand.Path
  foreach ($c in @("$here\understory", "$here\..\build\stage\understory")) { if (Test-Path "$c\bin\64bit\understory.dll") { $From = $c; break } }
}
if (-not $From -or -not (Test-Path "$From\bin\64bit\understory.dll")) { throw "could not find understory\bin\64bit\understory.dll" }
$dest = "$env:ProgramData\obs-studio\plugins\understory"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
Copy-Item -Recurse $From $dest
Write-Host "installed -> $dest  (restart OBS, then + > Understory (living nature))"
