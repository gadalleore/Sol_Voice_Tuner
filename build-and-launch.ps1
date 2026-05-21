#==============================================================================
# PitchCorrectorVST - Build and Launch Script
# Incremental build, copies VST3 into the "Shades" VST folder,
# launches/focuses Ableton Live.
#==============================================================================

param(
    [switch]$NoLaunch,   # -NoLaunch  : skip opening Ableton
    [switch]$Clean,      # -Clean     : nuke build/ first
    [switch]$Reconfigure # -Reconfigure : force a fresh CMake configure
)

$ErrorActionPreference = "Continue"
# Native commands (cmake, msbuild) write to stderr even on success; treat
# them as informational and use $LASTEXITCODE to detect real failure.
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $projectRoot

$Plugin     = "SolVoiceTuner"          # CMake target (no spaces)
$ProductName = "Sol Voice Tuner"        # PRODUCT_NAME -> .vst3 filename
$ShadesName = "Shades"                  # company sub-folder under each VST3 root
$Vst3Name   = "$ProductName.vst3"

# Older builds shipped under these names; clean them up so DAWs don't see ghosts
$LegacyVst3Names = @(
    "PitchCorrectorVST.vst3"
)

# ── Step 0: Optional clean ────────────────────────────────────────────────
if ($Clean -and (Test-Path "build")) {
    Write-Host "[$Plugin] -Clean: removing build/" -ForegroundColor Yellow
    Remove-Item -Recurse -Force build
}

# ── Step 1: CMake configure (only if cache missing or -Reconfigure) ───────
if ($Reconfigure -or -not (Test-Path "build\CMakeCache.txt")) {
    Write-Host "[$Plugin] Configuring CMake..." -ForegroundColor Yellow

    if (-not (Test-Path "build")) { New-Item -ItemType Directory -Path build | Out-Null }

    # Auto-detect installed Visual Studio via vswhere (most-recent first).
    $generators = @()
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -all -products * -property installationVersion 2>$null
        if ($found) {
            $majors = $found | ForEach-Object { ($_ -split '\.')[0] } | Sort-Object -Unique -Descending
            foreach ($m in $majors) {
                switch ($m) {
                    '18' { $generators += "Visual Studio 18 2026" }
                    '17' { $generators += "Visual Studio 17 2022" }
                    '16' { $generators += "Visual Studio 16 2019" }
                }
            }
        }
    }
    if ($generators.Count -eq 0) {
        # Fallback list if vswhere couldn't tell us anything useful
        $generators = @("Visual Studio 18 2026", "Visual Studio 17 2022")
    }
    Write-Host "[$Plugin] Generator candidates: $($generators -join ', ')" -ForegroundColor DarkGray

    $configured = $false
    foreach ($gen in $generators) {
        Write-Host "[$Plugin] Trying generator: $gen" -ForegroundColor Gray
        & cmake -S . -B build -G $gen -A x64
        if ($LASTEXITCODE -eq 0) { $configured = $true; break }
        # Wipe build dir and retry with the next generator
        Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Path build | Out-Null
    }
    if (-not $configured) {
        Write-Host "[$Plugin] CMake configure failed for all generators." -ForegroundColor Red
        exit 1
    }
    Write-Host "[$Plugin] CMake configured." -ForegroundColor Green
}

# ── Step 2: Incremental build ─────────────────────────────────────────────
Write-Host "[$Plugin] Building Release VST3..." -ForegroundColor Cyan
$buildStart = Get-Date
& cmake --build build --config Release --target "${Plugin}_VST3" --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host "[$Plugin] Build failed." -ForegroundColor Red
    exit 1
}
$buildSecs = [int]((Get-Date) - $buildStart).TotalSeconds
Write-Host "[$Plugin] Build succeeded in ${buildSecs}s." -ForegroundColor Green

# ── Step 3: Locate the VST3 artifact ──────────────────────────────────────
$source = "build\${Plugin}_artefacts\Release\VST3\${Vst3Name}"
if (-not (Test-Path $source)) {
    Write-Host "[$Plugin] VST3 artefact not found at: $source" -ForegroundColor Red
    exit 1
}

# ── Step 4: Copy VST3 into the "Shades" sub-folder of every known root ────
$vst3Roots = @(
    "C:\Program Files\Common Files\VST3",
    "$env:USERPROFILE\Documents\Ableton\User Library\VST3",
    "$env:USERPROFILE\Documents\VST3",
    "$env:USERPROFILE\VST3"
)

$copiedAny = $false
foreach ($root in $vst3Roots) {
    if (-not (Test-Path $root)) { continue }
    $shadesDir = Join-Path $root $ShadesName
    if (-not (Test-Path $shadesDir)) {
        try {
            New-Item -ItemType Directory -Path $shadesDir -Force | Out-Null
            Write-Host "[$Plugin] Created folder: $shadesDir" -ForegroundColor Gray
        } catch {
            Write-Host "[$Plugin] Skipping $shadesDir (could not create: $($_.Exception.Message))" -ForegroundColor DarkYellow
            continue
        }
    }

    # Clean up any legacy / renamed versions that may still be sitting in this folder
    foreach ($legacy in $LegacyVst3Names) {
        $legacyPath = Join-Path $shadesDir $legacy
        if (Test-Path $legacyPath) {
            try {
                Remove-Item -Recurse -Force $legacyPath -ErrorAction Stop
                Write-Host "[$Plugin] Removed stale: $legacyPath" -ForegroundColor DarkGray
            } catch {
                Write-Host "[$Plugin] Could not remove stale $legacyPath ($($_.Exception.Message))" -ForegroundColor DarkYellow
            }
        }
    }

    $dest = Join-Path $shadesDir $Vst3Name
    Write-Host "[$Plugin] Copying VST3 -> $dest" -ForegroundColor Cyan
    try {
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest -ErrorAction Stop }
        Copy-Item -Path $source -Destination $dest -Recurse -Force -ErrorAction Stop
        Write-Host "[$Plugin] OK -> $dest" -ForegroundColor Green
        $copiedAny = $true
    } catch {
        Write-Host "[$Plugin] Copy to $dest failed: $($_.Exception.Message)" -ForegroundColor DarkYellow
        Write-Host "[$Plugin]   (Tip: 'Program Files' targets need an admin shell.)" -ForegroundColor DarkGray
    }
}

if (-not $copiedAny) {
    Write-Host "[$Plugin] WARNING: No VST3 destination was writable." -ForegroundColor Yellow
}

# Brief settle so Ableton's plugin scanner sees a stable dir
Start-Sleep -Seconds 2

# ── Step 5: Launch or focus Ableton Live ──────────────────────────────────
if ($NoLaunch) {
    Write-Host "[$Plugin] Done. (-NoLaunch: skipping Ableton)" -ForegroundColor Gray
    exit 0
}

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like "Live*" }
if ($running) {
    Write-Host "[$Plugin] Ableton is running - bringing to front..." -ForegroundColor Green
    try {
        Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);
}
"@ -ErrorAction SilentlyContinue
        $proc = $running | Select-Object -First 1
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
            [Win32]::ShowWindowAsync($proc.MainWindowHandle, 9) | Out-Null  # SW_RESTORE
            [Win32]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
        }
    } catch { }
    Write-Host "[$Plugin] Rescan plugins in Ableton (Prefs > Plug-Ins > Rescan) to pick up the new build." -ForegroundColor Yellow
    exit 0
}

# Find Ableton executable
$abletonExe = $null
$roots = @(
    "C:\ProgramData\Ableton",
    "C:\Program Files\Ableton",
    (Join-Path ${env:ProgramFiles} "Ableton"),
    (Join-Path ${env:ProgramFiles(x86)} "Ableton")
) | Where-Object { $_ -and (Test-Path $_) }

# 1) Try most-recent versions first
$patterns = @()
foreach ($v in 12,11,10) {
    $patterns += "Live $v Suite\Program\Ableton Live $v Suite.exe"
    $patterns += "Live $v Standard\Program\Ableton Live $v Standard.exe"
    $patterns += "Live $v Intro\Program\Ableton Live $v Intro.exe"
    $patterns += "Live $v\Program\Ableton Live $v.exe"
}
foreach ($root in $roots) {
    foreach ($p in $patterns) {
        $c = Join-Path $root $p
        if (Test-Path $c) { $abletonExe = $c; break }
    }
    if ($abletonExe) { break }
}
# 2) Fallback: any Ableton Live*.exe under ...\Program\
if (-not $abletonExe) {
    foreach ($root in $roots) {
        $found = Get-ChildItem -Path $root -Recurse -Filter "Ableton Live*.exe" -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match "\\Program\\" } | Select-Object -First 1
        if ($found) { $abletonExe = $found.FullName; break }
    }
}

if (-not $abletonExe) {
    Write-Host "[$Plugin] Ableton Live not found. Edit \$abletonExe path in build-and-launch.ps1 or launch manually." -ForegroundColor Yellow
    exit 0
}

Write-Host "[$Plugin] Launching $abletonExe ..." -ForegroundColor Cyan
Start-Process -FilePath $abletonExe
Write-Host "[$Plugin] Ableton launched." -ForegroundColor Green
