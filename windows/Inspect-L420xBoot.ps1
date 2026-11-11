#requires -Version 5.1
<# Read-only audit of bydjPE media (does not modify any file). #>
[CmdletBinding()]
param([string]$Drive = 'E:')
$ErrorActionPreference = 'Stop'
$root = $Drive.TrimEnd('\') + '\'
$files = @(
  'efi\boot\pe.efi',
  'efi\boot\BOOTAA64.EFI',
  'efi\microsoft\boot\bootmgfw.efi',
  'efi\microsoft\boot\BCD',
  'sources\boot.wim',
  'boot\boot.sdi'
)
function Get-PeMachine([string]$path) {
  $s = [System.IO.File]::OpenRead($path)
  try {
    $br = New-Object System.IO.BinaryReader($s)
    if ($s.Length -lt 0x100 -or $br.ReadUInt16() -ne 0x5A4D) { return 'Not PE/MZ' }
    $s.Position = 0x3C
    $off = $br.ReadInt32()
    if ($off -lt 0 -or $off -gt ($s.Length - 24)) { return 'Invalid PE pointer' }
    $s.Position = $off
    if ($br.ReadUInt32() -ne 0x00004550) { return 'Not PE' }
    $machine = $br.ReadUInt16()
    switch ($machine) {
      0xAA64 { return 'ARM64 (0xAA64)' }
      0x8664 { return 'x64 (0x8664)' }
      0x14c { return 'x86 (0x014C)' }
      default { return ('Unknown (0x{0:X4})' -f $machine) }
    }
  } finally { $s.Dispose() }
}
$results = foreach ($name in $files) {
  $full = Join-Path $root $name
  $exists = Test-Path -LiteralPath $full -PathType Leaf
  $size = $null; $arch = ''; $hash = ''
  if ($exists) {
    $size = (Get-Item -LiteralPath $full).Length
    if ($name -match '\.efi$') {
      $arch = Get-PeMachine $full
      $hash = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
    }
  }
  [pscustomobject]@{Path=$name; Present=$exists; Bytes=$size; Machine=$arch; SHA256=$hash}
}
$results | Format-Table -AutoSize
$store = Join-Path $root 'efi\microsoft\boot\BCD'
if (Test-Path -LiteralPath $store) {
  Write-Host "[KD7] BCD: $store"
  & bcdedit.exe /store $store /enum '{bootmgr}'
  & bcdedit.exe /store $store /enum '{default}'
}
Write-Host '[KD7] No changes made. PE architecture checked using COFF Machine field.'
