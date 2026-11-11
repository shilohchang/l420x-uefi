#requires -Version 5.1
<#
.SYNOPSIS
  L420x KDNET USB EEM offline-BCD audit, guarded opt-in setup, and rollback.
.DESCRIPTION
  -Mode Audit (default) is READ ONLY.
  -Mode Enable requires a real, independently verified ARM USB-EEM capable
  DBG2 device and explicit busparams. It NEVER probes a target controller.
  -Mode Restore overwrites *only the specified offline BCD store* using a
  user-selected backup. Does not alter the host machine boot configuration.
  Tested by static inspection only; require target hardware validation.
.EXAMPLE
  .\Configure-L420xKDNET.ps1 -Mode Audit
.EXAMPLE
  .\Configure-L420xKDNET.ps1 -Mode Enable -BusParams 1 -ConfirmedDbg2UsbEntry
  Use ONLY after proving L420x device-mode Synopsys USB3 support, DBG2 entry 1,
  and availability of the Windows ARM KDNET-EEM transport binary.
#>
[CmdletBinding()]
param(
    [ValidateSet('Audit','Enable','Restore')]
    [string]$Mode = 'Audit',
    [string]$BcdStore = 'E:\efi\microsoft\boot\BCD',
    [ValidateRange(50000,50039)]
    [int]$Port = 50005,
    [int]$BusParams = -1,
    [switch]$ConfirmedDbg2UsbEntry,
    [string]$Key = '',
    [string]$BackupFile = ''
)
$ErrorActionPreference = 'Stop'

function Assert-OfflineStore {
    if (-not (Test-Path -LiteralPath $BcdStore -PathType Leaf)) {
        throw "找不到离线 BCD: $BcdStore"
    }
    $resolved = (Resolve-Path -LiteralPath $BcdStore).Path
    if ($resolved -notmatch '^[a-zA-Z]:\\') {
        throw '只接受带盘符的 Windows 本地/可移动离线 BCD 路径。'
    }
    if ($resolved.Substring(0,2).Equals($env:SystemDrive, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "为防止误改主机启动项，拒绝操作系统盘上的 BCD：$resolved"
    }
    return $resolved
}

function Invoke-Bcd([string[]]$Arguments) {
    $output = & "$env:SystemRoot\System32\bcdedit.exe" @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        # Do not echo a KEY:key argument even if the tool includes it in diagnostics.
        $msg = (($output | Out-String) -replace '(?i)key[:=]\s*[^\s]+', 'key:[REDACTED]')
        throw "BCDEdit exit=$LASTEXITCODE; $msg"
    }
    return $output
}

function New-KdKey {
    $alphabet = '0123456789abcdefghijklmnopqrstuvwxyz'
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $buf = New-Object byte[] 1
    try {
        $parts = @()
        for ($i = 0; $i -lt 4; $i++) {
            $part = ''
            while ($part.Length -lt 12) {
                $rng.GetBytes($buf)
                if ($buf[0] -lt 252) { $part += $alphabet[[int]($buf[0] % 36)] }
            }
            $parts += $part
        }
        return ($parts -join '.')
    } finally { $rng.Dispose() }
}

function Save-Key([string]$value, [string]$location) {
    [System.IO.File]::WriteAllText($location, $value, [System.Text.Encoding]::ASCII)
    $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = New-Object System.Security.AccessControl.FileSecurity
    $acl.SetAccessRuleProtection($true, $false)
    $rule = [System.Security.AccessControl.FileSystemAccessRule]::new($sid, [System.Security.AccessControl.FileSystemRights]::FullControl, [System.Security.AccessControl.AccessControlType]::Allow)
    $acl.AddAccessRule($rule)
    Set-Acl -LiteralPath $location -AclObject $acl
}

$store = Assert-OfflineStore
if ($Mode -eq 'Audit') {
    Write-Host "[KD7] READ-ONLY audit: $store"
    Write-Host '--- /enum {bootmgr} ---'
    Invoke-Bcd @('/store',$store,'/enum','{bootmgr}') | Out-Host
    Write-Host '--- /enum {default} ---'
    Invoke-Bcd @('/store',$store,'/enum','{default}') | Out-Host
    Write-Host '--- /dbgsettings (key redacted) ---'
    $dbg = (Invoke-Bcd @('/store',$store,'/dbgsettings') | Out-String)
    $dbg = $dbg -replace '(?im)^\s*(key|密钥)\s+.*$', 'key [REDACTED]'
    Write-Host $dbg
    Write-Host '[KD7] No BCD changes made.'
    exit 0
}

if ($Mode -eq 'Restore') {
    if ([string]::IsNullOrWhiteSpace($BackupFile) -or -not (Test-Path -LiteralPath $BackupFile -PathType Leaf)) {
        throw 'Restore requires -BackupFile pointing to a real backup.'
    }
    $backup = (Resolve-Path -LiteralPath $BackupFile).Path
    if (-not $backup.ToLowerInvariant().StartsWith((Split-Path $store -Parent).ToLowerInvariant() + '\')) {
        throw '备份必须位于当前离线 BCD 同一目录。'
    }
    Copy-Item -LiteralPath $backup -Destination $store -Force
    Write-Host "[KD7] Restored offline BCD from: $backup"
    exit 0
}

# An unbootable target cannot run kdnet.exe to probe the controller; independent
# hardware, firmware and transport verification is absolutely required.
if (-not $ConfirmedDbg2UsbEntry) {
    throw '拒绝启用：必须确认 L420x 的 USB3 Device Mode、ACPI DBG2 对应项及 ARM KDNET 传输支持后，显式加 -ConfirmedDbg2UsbEntry。'
}
if ($BusParams -lt 0 -or $BusParams -gt 127) {
    throw '拒绝启用：必须指定已经核实的 DBG2 表项编号，例如 -BusParams 1。不要猜。'
}
if ($Key -and $Key -cnotmatch '^([a-z0-9]{1,13}\.){3}[a-z0-9]{1,13}$') {
    throw 'KEY 格式无效：需要四段小写字母/数字的 base36 KDNET 密钥。'
}
if (-not $Key) { $Key = New-KdKey }
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = "$store.v7-kdnet-$timestamp.bak"
Copy-Item -LiteralPath $store -Destination $backup
$before = (Get-FileHash -LiteralPath $store -Algorithm SHA256).Hash
Write-Host "[KD7] Backed up offline BCD: $backup"
Write-Host "[KD7] Original SHA256: $before"
try {
    # ARM USB EEM transport, not ordinary xHCI KDUSB. Host IP per Microsoft.
    Invoke-Bcd @('/store',$store,'/dbgsettings','NET', 'HOSTIP:169.254.255.255', "PORT:$Port", "KEY:$Key", 'NODHCP') | Out-Null
    Invoke-Bcd @('/store',$store,'/set','{dbgsettings}','busparams',"$BusParams") | Out-Null
    Invoke-Bcd @('/store',$store,'/bootdebug','{bootmgr}','on') | Out-Null
    Invoke-Bcd @('/store',$store,'/bootdebug','{default}','on') | Out-Null
    Invoke-Bcd @('/store',$store,'/debug','{default}','on') | Out-Null

    $keyDir = Join-Path $env:USERPROFILE 'Documents\L420X-KDNET'
    New-Item -ItemType Directory -Path $keyDir -Force | Out-Null
    $keyFile = Join-Path $keyDir "kdnet-$timestamp.key"
    Save-Key $Key $keyFile
    Write-Host "[KD7] Configured only $store"
    Write-Host "[KD7] busparams=$BusParams port=$Port hostip=169.254.255.255"
    Write-Host "[KD7] KDNET key saved locally: $keyFile"
    Write-Host '[KD7] Do not publish your BCD or key file; both contain the debug secret.'
    Write-Host "[KD7] Roll back: .\Configure-L420xKDNET.ps1 -Mode Restore -BcdStore '$store' -BackupFile '$backup'"
} catch {
    Copy-Item -LiteralPath $backup -Destination $store -Force
    throw "Configuration failed; BCD restored from backup. $($_.Exception.Message)"
}
