# ============================================================================
#  build_manual.ps1 - build driver TRUC TIEP bang cl.exe + link.exe
#  Khong dung MSBuild / PlatformToolset => khong phu thuoc WDK-VS integration.
#
#     cd C:\sec_ts_y79a
#     powershell -ExecutionPolicy Bypass -File build_manual.ps1
# ============================================================================
$ErrorActionPreference = 'Continue'
$src = $PSScriptRoot
$vs  = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$lines = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $lines.Add([string]$s); Write-Host $s }

Say "=== build_manual sec_ts_y79a ==="
Say ("src = " + $src)

# ---- 1. tim cl.exe / link.exe target ARM64 ------------------------------------
Say ""
Say "--- 1. toolchain ---"
$msvcRoot = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
if (-not $msvcRoot) { Say "!! khong thay VC\Tools\MSVC"; return }
Say ("   MSVC: " + $msvcRoot.FullName)

$cl   = Join-Path $msvcRoot.FullName 'bin\Hostx64\arm64\cl.exe'
$link = Join-Path $msvcRoot.FullName 'bin\Hostx64\arm64\link.exe'
Say ("   cl   = " + $cl + "  exists=" + (Test-Path $cl))
Say ("   link = " + $link + "  exists=" + (Test-Path $link))
if (-not (Test-Path $cl)) { Say "!! thieu cl.exe ARM64"; return }

# ---- 2. SDK / WDK version ------------------------------------------------------
Say ""
Say "--- 2. SDK / WDK ---"
$sdkVer = Get-ChildItem "$kits\Include" -Directory -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -match '^10\.' } | Sort-Object Name -Descending | Select-Object -First 1
$wdkVer = Get-ChildItem "$kits\Include" -Directory -ErrorAction SilentlyContinue |
          Where-Object { Test-Path (Join-Path $_.FullName 'km\wdm.h') } | Sort-Object Name -Descending | Select-Object -First 1
Say ("   SDK Include ver = " + $sdkVer.Name)
Say ("   WDK km ver      = " + $wdkVer.Name)

$incKits = @(
    "$kits\Include\$($sdkVer.Name)\km",
    "$kits\Include\$($sdkVer.Name)\shared",
    "$kits\Include\$($sdkVer.Name)\ucrt",
    "$kits\Include\$($sdkVer.Name)\um",
    "$kits\Include\$($sdkVer.Name)\winrt",
    "$kits\Include\wdf",
    "$kits\Include\$($sdkVer.Name)\km\wdf"
) | Where-Object { Test-Path $_ }

$libKits = @(
    "$kits\Lib\$($sdkVer.Name)\km\arm64",
    "$kits\Lib\$($sdkVer.Name)\ucrt\arm64",
    "$kits\Lib\$($sdkVer.Name)\um\arm64"
) | Where-Object { Test-Path $_ }

Say "   include dirs:"
$incKits | ForEach-Object { Say ("      " + $_) }
Say "   lib dirs:"
$libKits | ForEach-Object { Say ("      " + $_) }

$msvcInc = @(
    "$($msvcRoot.FullName)\include",
    "$($msvcRoot.FullName)\include\ucrt",
    "$($msvcRoot.FullName)\include\msvc"
) | Where-Object { Test-Path $_ }
Say "   msvc include:"
$msvcInc | ForEach-Object { Say ("      " + $_) }

$msvcLib = @(
    "$($msvcRoot.FullName)\lib\arm64",
    "$($msvcRoot.FullName)\lib\arm64\spectre"
) | Where-Object { Test-Path $_ }
Say "   msvc lib:"
$msvcLib | ForEach-Object { Say ("      " + $_) }

# ---- 3. build ------------------------------------------------------------------
Say ""
Say "--- 3. compile ---"
$objDir = Join-Path $src 'obj'
New-Item -ItemType Directory -Path $objDir -Force | Out-Null

$inc = ($msvcInc + $incKits) | ForEach-Object { "/I`"$_`"" }
$defs = @(
    '/D_WIN64', '/DWIN64', '/DAMD64', '/D_AMD64_',
    '/DPOOL_NX_OPTIN=1',
    '/DKMDF_VERSION_MAJOR=1', '/DKMDF_VERSION_MINOR=33',
    '/D_WIN32_WINNT=0x0A00', '/DNTDDI_VERSION=0x0A00000C',
    '/DDBG=0'
)
$cflags = @(
    '/nologo', '/c', '/O2', '/W3', '/GS-', '/Gz', '/Zl',
    '/kernel', '/GR-', '/EHs-', '/utf-8',
    '/Fo"' + $objDir + '\\"'
)

$srcFiles = @('sec_ts_y79a.c', 'sec_ts_hw.c', 'sec_ts_chip.c', 'sec_ts_hid.c', 'sec_ts_poll.c')
$objs = @()
$fail = $false
foreach ($f in $srcFiles) {
    $obj = Join-Path $objDir ($f -replace '\.c$', '.obj')
    $cmd = @($cl) + $cflags + $defs + $inc + @("`"$(Join-Path $src $f)`"", "/Fo`"$obj`"")
    Say ("   " + $f)
    $o = & $cl @cflags $defs $inc (Join-Path $src $f) "/Fo$obj" 2>&1
    $o | ForEach-Object { Say ("      " + $_) }
    if (Test-Path $obj) { $objs += $obj } else { $fail = $true }
}

if ($fail) {
    Say ""
    Say "!! co file loi bien dich, xem log tren"
    $lines | Out-File (Join-Path $src 'build_manual_log.txt') -Encoding utf8
    return
}

Say ""
Say "--- 4. link ---"
$outSys = Join-Path $src 'sec_ts_y79a.sys'
$ldflags = @(
    '/nologo', '/DRIVER', '/SUBSYSTEM:NATIVE', '/MACHINE:ARM64',
    '/KERNEL', '/NXCOMPAT', '/INTEGRITYCHECK',
    '/ENTRY:DriverEntry', '/OPT:REF', '/OPT:ICF', '/INCREMENTAL:NO'
)
$libdirs = ($msvcLib + $libKits) | ForEach-Object { "/LIBPATH:`"$_`"" }
$libs = @('ntoskrnl.lib', 'hal.lib', 'wmilib.lib', 'BufferOverflowK.lib',
          'wdmsec.lib', 'WdfLdr.lib', 'WdfDriverEntry.lib', 'vhfkm.lib')

$ld = & $link @ldflags $libdirs @objs "/OUT:$outSys" @libs 2>&1
$ld | ForEach-Object { Say ("   " + $_) }

Say ""
Say "--- 5. ket qua ---"
if (Test-Path $outSys) {
    $fi = Get-Item $outSys
    Say ("   OK: " + $fi.FullName + "  " + $fi.Length + " B")
} else {
    Say "   !! chua tao duoc .sys"
}
$lines | Out-File (Join-Path $src 'build_manual_log.txt') -Encoding utf8
Say "Da ghi build_manual_log.txt"