#requires -Version 5.1
<# Host-side WinDbg launcher. Only use after configuring supported target transport. #>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$KeyFile,
  [ValidateRange(50000,50039)][int]$Port = 50005,
  [string]$WinDbgPath = ''
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $KeyFile -PathType Leaf)) { throw 'No KDNET key file' }
$key = (Get-Content -LiteralPath $KeyFile -Raw).Trim()
if ($key -cnotmatch '^([a-z0-9]{1,13}\.){3}[a-z0-9]{1,13}$') {
  throw 'Invalid KDNET key format'
}
if (-not $WinDbgPath) {
  $candidates = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64\windbg.exe",
    "$env:ProgramFiles\Windows Kits\10\Debuggers\x64\windbg.exe"
  )
  $WinDbgPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $WinDbgPath -or -not (Test-Path -LiteralPath $WinDbgPath)) {
  throw 'WinDbg executable not found. Supply -WinDbgPath, or install Windows Debugging Tools.'
}
Write-Host "[KD7] Launching host WinDbg on NET port $Port (key not printed)."
& $WinDbgPath -k "net:port=$Port,key=$key"
