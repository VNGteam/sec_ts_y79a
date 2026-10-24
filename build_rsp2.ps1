# ============================================================================
#  build_rsp2.ps1 - sua 2 loi: (1) duong dan wdf.h nam trong wdf\kmdf\<ver>
#                            (2) define ARM64 dung (khong dung _AMD64_)
#     cd C:\sec_ts_y79a
#     powershell -ExecutionPolicy Bypass -File build_rsp2.ps1
# ============================================================================
$ErrorActionPreference = 'Continue'
$src  = $PSScriptRoot
$vs   = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$lines = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $lines.Add([string]$s); Write-Host $s }

Say "=== build_rsp2 sec_ts_y79a ==="

$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
$cl   = Join-Path $msvc.FullName 'bin\Hostx64\arm64\cl.exe'
$link = Join-Path $msvc.FullName 'bin\Hostx64\arm64\link.exe'
Say ("cl   exists=" + (Test-Path $cl))
Say ("link exists=" + (Test-Path $link))
if (-not (Test-Path $cl)) { return }

# ---- tim wdf.h dung cho (wdf\kmdf\<ver>\wdf.h) --------------------------------
Say ""
Say "--- tim wdf.h ---"
$wdfDir = $null
$wdfRoot = Join-Path $kits 'Include\wdf'
if (Test-Path $wdfRoot) {
    # CHI lay kmdf (khong lay umdf), version cao nhat
    $kmdf = Get-ChildItem (Join-Path $wdfRoot 'kmdf') -Recurse -Filter wdf.h -ErrorAction SilentlyContinue |
            ForEach-Object {
                $ver = $null
                if ($_.Directory.Name -match '^(\d+)\.(\d+)$') {
                    $ver = [version]($_.Directory.Name)
                }
                [pscustomobject]@{ Path = $_.FullName; Dir = $_.DirectoryName; Ver = $ver }
            } | Where-Object { $_.Ver } | Sort-Object Ver -Descending
    $kmdf | ForEach-Object { Say ("   kmdf " + $_.Ver + "  " + $_.Path) }
    if ($kmdf) { $wdfDir = $kmdf[0].Dir }
}
if (-not $wdfDir) {
    # du phong: tim trong toan bo Windows Kits\10\Include
    $c = Get-ChildItem (Join-Path $kits 'Include') -Recurse -Filter wdf.h -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($c) { $wdfDir = $c.DirectoryName }
}
Say ("   => wdfDir = " + $wdfDir)
if (-not $wdfDir) { Say "!! khong tim thay wdf.h (KMDF headers chua cai)"; return }

$kmdfVer = Split-Path $wdfDir -Leaf
Say ("   kmdf version = " + $kmdfVer)

$sdk = Get-ChildItem "$kits\Include" -Directory -ErrorAction SilentlyContinue |
       Where-Object { Test-Path (Join-Path $_.FullName 'km\wdm.h') } |
       Sort-Object Name -Descending | Select-Object -First 1
Say ("   wdk ver = " + $sdk.Name)

$incs = @(
    "$($msvc.FullName)\include",
    "$($msvc.FullName)\include\ucrt",
    "$($msvc.FullName)\include\msvc",
    "$kits\Include\$($sdk.Name)\km",
    "$kits\Include\$($sdk.Name)\shared",
    "$kits\Include\$($sdk.Name)\ucrt",
    "$kits\Include\$($sdk.Name)\um",
    $wdfDir
) | Where-Object { Test-Path $_ }

# WDF libs nam o:  Lib\wdf\kmdf\arm64\<x.y>\   (ARCH truoc, VERSION sau)
$wdfLibDirs = @()
$wdfArchRoot = Join-Path $kits 'Lib\wdf\kmdf\arm64'
if (Test-Path $wdfArchRoot) {
    $wdfLibDirs = Get-ChildItem $wdfArchRoot -Directory -ErrorAction SilentlyContinue |
                  ForEach-Object {
                      $v = $null
                      if ($_.Name -match '^(\d+)\.(\d+)$') { $v = [version]$_.Name }
                      [pscustomobject]@{ Dir = $_.FullName; Ver = $v }
                  } | Where-Object { $_.Ver } |
                  Sort-Object Ver -Descending | ForEach-Object { $_.Dir }
}

$libs = @(
    "$($msvc.FullName)\lib\arm64",
    "$kits\Lib\$($sdk.Name)\km\arm64",
    "$kits\Lib\$($sdk.Name)\ucrt\arm64",
    "$kits\Lib\$($sdk.Name)\um\arm64"
) + $wdfLibDirs | Where-Object { Test-Path $_ } | Select-Object -Unique

Say ""
Say "include:"
$incs | ForEach-Object { Say ("   " + $_) }
Say "lib:"
$libs | ForEach-Object { Say ("   " + $_) }
Say "wdf lib dirs:"
if ($wdfLibDirs) { $wdfLibDirs | ForEach-Object { Say ("   " + $_) } } else { Say "   <khong thay>" }

$objDir = Join-Path $src 'obj'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null

# ---- compile -------------------------------------------------------------------
Say ""
Say "--- compile ---"
$srcFiles = @('sec_ts_y79a.c', 'sec_ts_hw.c', 'sec_ts_chip.c', 'sec_ts_hid.c', 'sec_ts_poll.c')
$objs = @()
$failed = $false

foreach ($f in $srcFiles) {
    $obj = Join-Path $objDir ($f -replace '\.c$', '.obj')
    $rsp = Join-Path $objDir ($f -replace '\.c$', '.rsp')

    $r = New-Object System.Collections.Generic.List[string]
    $r.Add('/nologo'); $r.Add('/c'); $r.Add('/O2'); $r.Add('/W3')
    # BAT GS (mac dinh) de co __security_* runtime; BAT /Oi de inline intrinsic
    $r.Add('/Oi')
    $r.Add('/Gz'); $r.Add('/Zl')
    $r.Add('/kernel'); $r.Add('/GR-'); $r.Add('/EHs-c-'); $r.Add('/utf-8')
    # ---- ARM64 defines (KHONG dung _AMD64_/AMD64) ----
    # ntdef.h can _ARM64_ (WDK build system dat); compiler tu co _M_ARM64
    $r.Add('/D_ARM64_')
    $r.Add('/D_WIN64')
    $r.Add('/DPOOL_NX_OPTIN=1')
    $r.Add('/DKMDF_VERSION_MAJOR=1')
    $r.Add('/DKMDF_VERSION_MINOR=33')
    $r.Add('/D_WIN32_WINNT=0x0A00')
    $r.Add('/DNTDDI_VERSION=0x0A00000C')
    # ntstrsafe: bao dung che do kernel de khong keo __stdio_common_vswprintf
    $r.Add('/DNTSTRSAFE_LIB')
    $r.Add('/DNTSTRSAFE_NO_CB_FUNCTIONS=0')
    foreach ($i in $incs) { $r.Add('/I"' + $i + '"') }
    $r.Add('/Fo"' + $obj + '"')
    $r.Add('"' + (Join-Path $src $f) + '"')
    Set-Content -LiteralPath $rsp -Value $r -Encoding ASCII

    Say ("   " + $f)
    $o = & $cl ('@' + $rsp) 2>&1
    # chi in dong loi / warning quan trong, bo qua wdm.h warning
    $o | Where-Object { $_ -notmatch 'wdm\.h\(' } | ForEach-Object { Say ("      " + $_) }
    if (Test-Path $obj) { $objs += $obj } else { $failed = $true }
}

if ($failed) {
    Say ""
    Say "!! con loi bien dich"
    $lines | Out-File (Join-Path $src 'build_rsp2_log.txt') -Encoding utf8
    return
}

# ---- link ----------------------------------------------------------------------
Say ""
Say "--- link ---"
$outSys = Join-Path $src 'sec_ts_y79a.sys'
$lrsp = Join-Path $objDir 'link.rsp'
$l = New-Object System.Collections.Generic.List[string]
$l.Add('/nologo'); $l.Add('/DRIVER'); $l.Add('/SUBSYSTEM:NATIVE')
$l.Add('/MACHINE:ARM64'); $l.Add('/KERNEL'); $l.Add('/NXCOMPAT')
# KHONG dat /ENTRY:DriverEntry - WdfDriverEntry.lib cung cap FxDriverEntry,
# linker se tu chon entry point dung cho KMDF
$l.Add('/OPT:REF'); $l.Add('/OPT:ICF')
$l.Add('/INCREMENTAL:NO')
foreach ($d in $libs) { $l.Add('/LIBPATH:"' + $d + '"') }
foreach ($o in $objs) { $l.Add('"' + $o + '"') }
$l.Add('"/OUT:' + $outSys + '"')
foreach ($lib in @('ntoskrnl.lib','hal.lib','wmilib.lib',
                   'wdmsec.lib','ntstrsafe.lib',
                   'WdfDriverEntry.lib','WdfLdr.lib',
                   'vhfkm.lib','BufferOverflowFastFailK.lib')) {
    $l.Add($lib)
}
Set-Content -LiteralPath $lrsp -Value $l -Encoding ASCII

# kiem tra tung lib co ton tai khong (giup chan doan nhanh)
Say "   --- kiem tra lib ---"
$vhfFound = $false
foreach ($d in $libs) { if (Test-Path (Join-Path $d 'vhfkm.lib')) { $vhfFound = $true } }
if (-not $vhfFound) {
    Say "   !! THIEU vhfkm.lib (VHF). Cac lib co trong Lib\...\km\arm64:"
    foreach ($d in $libs) {
        if ($d -match 'km\\arm64') {
            Get-ChildItem $d -Filter '*.lib' -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match 'vhf|wdf|ntos|hal|wmilib|wdmsec' } |
                ForEach-Object { Say ("        " + $_.Name) }
        }
    }
}
foreach ($lib in @('ntoskrnl.lib','hal.lib','wmilib.lib','wdmsec.lib','ntstrsafe.lib',
                   'WdfLdr.lib','WdfDriverEntry.lib','vhfkm.lib','BufferOverflowFastFailK.lib')) {
    $hit = $null
    foreach ($d in $libs) { if (Test-Path (Join-Path $d $lib)) { $hit = Join-Path $d $lib; break } }
    if ($hit) { Say ("      OK   " + $lib) } else { Say ("      THIEU " + $lib) }
}

$lo = & $link ('@' + $lrsp) 2>&1
$lo | ForEach-Object { Say ("   " + $_) }

Say ""
Say "--- ket qua ---"
if (Test-Path $outSys) { Say ("   OK: " + $outSys + "  " + (Get-Item $outSys).Length + " B") }
else { Say "   !! chua tao duoc .sys" }

$lines | Out-File (Join-Path $src 'build_rsp2_log.txt') -Encoding utf8
Say "Da ghi build_rsp2_log.txt"