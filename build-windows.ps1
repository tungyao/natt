#Requires -Version 5.1
<#
.SYNOPSIS
    NATMesh Windows one-click build (env setup + compile)
.DESCRIPTION
    Auto-install MSYS2 UCRT64 + dependencies, then CMake + MinGW build.
    Safe to re-run - skips already-installed packages.
#>

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir    = Join-Path $ProjectRoot "build-windows"

function step  { Write-Host "`n--- $args ---" -ForegroundColor Cyan }
function ok    { Write-Host "  [OK] $args" -ForegroundColor Green }
function warn  { Write-Host "  [!] $args" -ForegroundColor Yellow }
function fail  { Write-Host "  [FAIL] $args" -ForegroundColor Red }

# ── 1. MSYS2 ─────────────────────────────────────────────
step "1/5 Checking MSYS2"

$Msys2Root = "C:\msys64"
$Pacman   = "$Msys2Root\usr\bin\pacman.exe"
$LockFile = "$Msys2Root\var\lib\pacman\db.lck"

# Clean stale lock file if no pacman running
if (Test-Path $LockFile) {
    $ps = Get-Process pacman -ErrorAction SilentlyContinue
    if (-not $ps) { Remove-Item $LockFile -Force }
}

if (-not (Test-Path $Pacman)) {
    warn "MSYS2 not found, installing via winget..."
    $p = Start-Process winget -ArgumentList @(
        "install", "--id", "MSYS2.MSYS2",
        "--accept-source-agreements", "--silent"
    ) -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) {
        fail "MSYS2 install failed (exit $($p.ExitCode))"
        exit 1
    }
    ok "MSYS2 installed"
} else {
    ok "MSYS2 ready at $Msys2Root"
}

# ── 2. Update package DB (optional) ──────────────────────
step "2/5 Updating package DB"
& $Pacman -Sy --noconfirm 2>$null
if ($LASTEXITCODE -ne 0) {
    warn "DB update timed out (using local cache)"
} else {
    ok "Package DB updated"
}

# ── 3. Install dependencies ──────────────────────────────
step "3/5 Installing build dependencies"

$Packages = @(
    "mingw-w64-ucrt-x86_64-gcc"
    "mingw-w64-ucrt-x86_64-boost"
    "mingw-w64-ucrt-x86_64-yaml-cpp"
    "mingw-w64-ucrt-x86_64-sqlite3"
    "mingw-w64-ucrt-x86_64-cmake"
    "mingw-w64-ucrt-x86_64-make"
)

$Missing = @()
foreach ($pkg in $Packages) {
    & $Pacman -Q $pkg 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { $Missing += $pkg }
}

if ($Missing.Count -eq 0) {
    ok "All dependencies already installed"
} else {
    Write-Host "  -> Installing $($Missing.Count) packages..." -ForegroundColor Yellow
    & $Pacman -S --noconfirm --needed @Missing
    if ($LASTEXITCODE -ne 0) {
        fail "Package install failed"
        exit 1
    }
    ok "Dependencies installed"
}

# ── 4. CMake configure ──────────────────────────────────
step "4/5 CMake configure"
$Ucrt64Bash = "$Msys2Root\ucrt64.exe"
$SrcUnix = $ProjectRoot -replace "\\", "/"

& $Ucrt64Bash -c "cd '$SrcUnix' && cmake -G 'MinGW Makefiles' -S . -B build-windows -DCMAKE_BUILD_TYPE=Release" 2>&1
if ($LASTEXITCODE -ne 0) { fail "CMake configure failed"; exit 1 }
ok "CMake configured"

# ── 5. Build ─────────────────────────────────────────────
step "5/5 Building (parallel)"
$Cpus = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
& $Ucrt64Bash -c "cd '$SrcUnix' && mingw32-make -C build-windows -j$Cpus" 2>&1
if ($LASTEXITCODE -ne 0) { fail "Build failed"; exit 1 }

# ── Done ─────────────────────────────────────────────────
step "Done! All targets built"

$Exes = @("natmesh-server.exe","relay_server.exe","stun_server.exe","test_client.exe")
foreach ($exe in $Exes) {
    $path = Join-Path $BuildDir $exe
    if (Test-Path $path) {
        $size = (Get-Item $path).Length / 1MB
        ok ("{0,-30} {1,5:F1} MB" -f $exe, $size)
    }
}

Write-Host "`nBuild directory: $BuildDir" -ForegroundColor Cyan
Write-Host "Run examples:" -ForegroundColor Cyan
Write-Host "  stun_server.exe   --listen 0.0.0.0 --port 3478" -ForegroundColor Gray
Write-Host "  natmesh-server.exe config.yaml" -ForegroundColor Gray
Write-Host "  relay_server.exe   --port 7000" -ForegroundColor Gray
Write-Host "  test_client.exe    --help" -ForegroundColor Gray
