# =====================================================================
# ASPECTRA X - MASTER BUILD, DEPLOY & AUTO-SYNC PIPELINE (VERBOSE)
# =====================================================================
$ErrorActionPreference = "Stop"

Write-Host "[1/5] Stopping running Aspectra instances..." -ForegroundColor Cyan
$runningProcesses = Get-Process -Name "Aspectra", "Aspectra X" -ErrorAction SilentlyContinue
if ($runningProcesses) {
    foreach ($proc in $runningProcesses) {
        Write-Host "   -> Stopping process ID: $($proc.Id)" -ForegroundColor Yellow
        Stop-Process -InputObject $proc -Force
    }
} else {
    Write-Host "   -> No active Aspectra instances found." -ForegroundColor DarkGray
}

Write-Host "[2/5] Compiling via Ninja (Release mode)..." -ForegroundColor Cyan
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cm = 'C:\Users\rmart\Documents\Codex\2026-08-30\how-else-can-i-expand-this\work\take_slicer\vendor\vcpkg\downloads\tools\cmake-4.4.2-windows\cmake-4.4.2-windows-x86_64\bin\cmake.exe'
$build = 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\work\aspectra-release-ninja'

Write-Host "   -> Initializing MSVC environment and streaming Ninja build output..." -ForegroundColor DarkGray
# Removed '>nul' so you can see every compiled object file scroll by live in the terminal
$buildCmd = "call `"$vc`" && `"$cm`" --build `"$build`" --config Release --verbose"
cmd /c $buildCmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[3/5] Inspecting dependencies with dumpbin..." -ForegroundColor Cyan
$source = "$build\Aspectra.exe"
$targetDir = 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra X'
$target = "$targetDir\Aspectra.exe"
$dump = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
$deploy = 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\work\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'

Write-Host "   -> Checking for forbidden debug DLL dependencies..." -ForegroundColor DarkGray
$deps = & $dump /dependents $source
if ($deps -match '(?im)^\s*(Qt6\S*d\.dll|MSVCRTD\.dll|ucrtbased\.dll)\s*$') {
    throw "Fatal: Debug dependency found in Release build!"
}
Write-Host "   -> Dependency check passed (Clean Release binaries only)." -ForegroundColor Green

Write-Host "[4/5] Deploying canonical executable & running windeployqt..." -ForegroundColor Cyan
if (!(Test-Path $targetDir)) { 
    Write-Host "   -> Creating target directory: $targetDir" -ForegroundColor DarkGray
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null 
}
Write-Host "   -> Copying fresh executable..." -ForegroundColor DarkGray
Copy-Item -LiteralPath $source -Destination $target -Force

Write-Host "   -> Running windeployqt to bundle required Qt runtime plugins..." -ForegroundColor DarkGray
& $deploy --release --compiler-runtime --force --no-translations $target

Write-Host "[5/5] Re-launching Aspectra X & triggering background git sync..." -ForegroundColor Green

Write-Host "   -> Launching: $target" -ForegroundColor DarkGray
& $target

# Fire-and-forget headless git background sync with output tracing
Start-Job -ScriptBlock {
    Set-Location 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra'
    git add --all
    git diff --cached --quiet
    if ($LASTEXITCODE -ne 0) {
        $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        git commit -m "Auto-sync: $timestamp"
        git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=30 push origin main
    }
} | Out-Null

Write-Host "Pipeline complete. App is live and syncing!" -ForegroundColor Green