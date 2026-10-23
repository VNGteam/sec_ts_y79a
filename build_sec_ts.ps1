# ============================================================================
#  build_sec_ts.ps1 - build + sign driver cam ung sec_ts_y79a (chay trong VM)
#
#  Yeu cau: VS 2022 + WDK da cai (ban da co).
#  Chay tu thu muc chua source (sec_ts_y79a.*):
#     powershell -ExecutionPolicy Bypass -File build_sec_ts.ps1
#
#  Ket qua: .\build\ARM64\Release\sec_ts_y79a.sys + .cat da ky
#           va san sang de copy sang C:\y2q_try\drivers\sec_ts_y79a\
# ============================================================================
$ErrorActionPreference = 'Continue'
$src = $PSScriptRoot
$out = Join-Path $src 'build\ARM64\Release'
$lines = New-Object System.Collections.Generic.List[string]
function W($s) { $lines.Add([string]$s); Write-Host $s }

W "=== build sec_ts_y79a ==="
W ("src: " + $src)

# ---- 1. tim msbuild ------------------------------------------------------------
W ""
W "--- 1. tim msbuild ---"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                          -find MSBuild\**\Bin\MSBuild.exe 2>$null | Select-Object -First 1
}
if (-not $msbuild) {
    $cands = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    )
    $msbuild = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
}
W ("msbuild = " + $msbuild)
if (-not $msbuild) {
    W "!! khong tim thay MSBuild.exe. Cai 'Visual Studio 2022' voi workload 'Desktop development with C++' + WDK."
    $lines | Out-File (Join-Path $src 'build_log.txt') -Encoding utf8
    return
}

# ---- 2. build ------------------------------------------------------------------
W ""
W "--- 2. msbuild (Release ARM64) ---"
$proj = Join-Path $src 'sec_ts_y79a.vcxproj'
$msbOut = & $msbuild $proj /p:Configuration=Release /p:Platform=ARM64 /t:Build /v:minimal /nologo 2>&1
$msbOut | ForEach-Object { W ("   " + $_) }
W ("exit code = " + $LASTEXITCODE)

# ---- 3. kiem tra output --------------------------------------------------------
W ""
W "--- 3. output ---"
$sys = Join-Path $out 'sec_ts_y79a.sys'
if (Test-Path $sys) { W ("   sys: " + $sys + "  " + (Get-Item $sys).Length + " B") }
else {
    W "   !! chua co .sys. Thu build bang chinh Visual Studio (mo sec_ts_y79a.vcxproj) neu msbuild loi."
    Get-ChildItem $src -Recurse -Include *.sys -ErrorAction SilentlyContinue |
        ForEach-Object { W ("   tim thay: " + $_.FullName) }
}

# ---- 4. ky ---------------------------------------------------------------------
W ""
W "--- 4. ky driver ---"
if (Test-Path $sys) {
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=Y2Q Test Driver Signing' } | Select-Object -First 1
    if (-not $cert) {
        W "   !! khong thay cert 'CN=Y2Q Test Driver Signing' trong store cua VM."
        W "      Chay lai planB5.ps1 de tao cert, hoac import C:\y2q_pkg\y2q_test.pfx."
    } else {
        W ("   cert: " + $cert.Thumbprint)
        $pfx = 'C:\y2q_pkg\y2q_test.pfx'
        $signtool = $null
        foreach ($r in @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")) {
            if (Test-Path $r) {
                $signtool = Get-ChildItem $r -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                            Where-Object { $_.FullName -match '\\arm64\\|\\x64\\|\\x86\\' } |
                            Select-Object -First 1 -ExpandProperty FullName
            }
            if ($signtool) { break }
        }
        W ("   signtool = " + $signtool)

        if ($signtool -and (Test-Path $pfx)) {
            # ky .sys (chữ ký nhúng)
            & $signtool sign /fd sha256 /f $pfx /p y2qpass /a /v $sys 2>&1 | ForEach-Object { W ("   " + $_) }

            # tao + ky catalog cho INF
            $inf2cat = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter Inf2Cat.exe -ErrorAction SilentlyContinue |
                       Select-Object -First 1 -ExpandProperty FullName
            W ("   inf2cat = " + $inf2cat)
            if ($inf2cat) {
                # inf2cat can ca .inf va .sys trong cung thu muc
                $pkgDir = Join-Path $src 'package'
                New-Item -ItemType Directory -Path $pkgDir -Force | Out-Null
                Copy-Item $sys (Join-Path $pkgDir 'sec_ts_y79a.sys') -Force
                Copy-Item (Join-Path $src 'sec_ts_y79a.inf') (Join-Path $pkgDir 'sec_ts_y79a.inf') -Force
                $cat = Join-Path $pkgDir 'sec_ts_y79a.cat'
                if (Test-Path $cat) { Remove-Item $cat -Force }
                $tok = '10_RS5_ARM64'
                & $inf2cat /driver:$pkgDir /os:$tok 2>&1 | ForEach-Object { W ("   " + $_) }
                if (Test-Path $cat) {
                    & $signtool sign /fd sha256 /f $pfx /p y2qpass /a /v $cat 2>&1 | ForEach-Object { W ("   " + $_) }
                    W ("   cat: " + $cat + "  " + (Get-Item $cat).Length + " B")
                } else { W "   !! inf2cat khong tao duoc cat" }
            }
        } else { W "   !! thieu signtool hoac pfx" }
    }
}

# ---- 5. tom tat -----------------------------------------------------------------
W ""
W "--- 5. ket qua ---"
foreach ($d in @($out, (Join-Path $src 'package'))) {
    if (Test-Path $d) {
        W ("   " + $d)
        Get-ChildItem $d -File | ForEach-Object { W ("      {0}  {1} B" -f $_.Name, $_.Length) }
    }
}
$lines | Out-File (Join-Path $src 'build_log.txt') -Encoding utf8
W ""
W "XONG. Gui lai build_log.txt + danh sach file trong package\ cho agent."