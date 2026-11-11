#requires -Version 5.1
<# Read-only mount of L420x bydjPE boot.wim; checks ARM kernel debug DLLs. #>
[CmdletBinding()]
param([string]$WimFile = 'E:\sources\boot.wim', [ValidateRange(1,99)][int]$Index = 1)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $WimFile -PathType Leaf)) {
    throw "找不到 WIM：$WimFile"
}
$mount = Join-Path $env:TEMP ('L420X-KD7-Mount-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $mount | Out-Null
$mounted = $false
try {
    & dism.exe /Mount-Image "/ImageFile:$WimFile" "/Index:$Index" "/MountDir:$mount" /ReadOnly
    if ($LASTEXITCODE -ne 0) { throw "DISM Mount-Image exit=$LASTEXITCODE" }
    $mounted = $true
    $sys = Join-Path $mount 'Windows\System32'
    Write-Host "[KD7] Read-only WIM index $Index at $mount"
    foreach ($name in @('kd_8003_5143.dll','kdnet.dll','kdcom.dll','winload.efi','ntoskrnl.exe')) {
        $file = Join-Path $sys $name
        Write-Host ('{0,-22} {1}' -f $name, (Test-Path -LiteralPath $file -PathType Leaf))
    }
    Write-Host '[KD7] Missing ARM EEM KD transport is a blocker until a compatible, legitimate target component is provided.'
    Write-Host '[KD7] Presence alone does not establish controller compatibility.'
}
finally {
    if ($mounted) {
        & dism.exe /Unmount-Image "/MountDir:$mount" /Discard | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "DISM unmount failed; inspect with dism /Get-MountedImageInfo: $mount"
        } else {
            Remove-Item -LiteralPath $mount -Force -ErrorAction SilentlyContinue
        }
    } else {
        # Failed mounts may leave a mount point registered; don't erase its contents.
        if (-not (Get-ChildItem -LiteralPath $mount -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) {
            Remove-Item -LiteralPath $mount -Force -ErrorAction SilentlyContinue
        }
    }
}
