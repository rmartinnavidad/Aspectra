# =====================================================================
# ASPECTRA X - FULL VISIBILITY & FAST DEPLOY PIPELINE
# =====================================================================
$ErrorActionPreference = "Stop"

Write-Host "`n[1/6] Stopping running Aspectra instances..." -ForegroundColor Cyan
$runningProcesses = Get-Process -Name "Aspectra", "Aspectra X" -ErrorAction SilentlyContinue
if ($runningProcesses) {
    foreach ($proc in $runningProcesses) {
        Write-Host "   -> Stopped process ID: $($proc.Id)" -ForegroundColor DarkGray
        Stop-Process -InputObject $proc -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "   -> No active Aspectra instances found." -ForegroundColor DarkGray
}

Write-Host "`n[2/6] Clearing stale locks..." -ForegroundColor Cyan
$lockFile = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\work\aspectra-release-ninja\CMakeFiles\Aspectra_autogen.dir\autogen.lock"
if (Test-Path $lockFile) {
    Remove-Item -Path $lockFile -Force -ErrorAction SilentlyContinue
    Write-Host "   -> Stale lock removed." -ForegroundColor Green
} else {
    Write-Host "   -> No stale locks detected." -ForegroundColor DarkGray
}

Write-Host "`n[3/6] Compiling via Ninja (Live Verbose Stream)..." -ForegroundColor Cyan
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$cm = 'C:\Users\rmart\Documents\Codex\2026-08-30\how-else-can-i-expand-this\work\take_slicer\vendor\vcpkg\downloads\tools\cmake-4.4.2-windows\cmake-4.4.2-windows-x86_64\bin\cmake.exe'
$build = 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\work\aspectra-release-ninja'

# We write the command to a temporary batch script and run it natively. 
# This completely bypasses PowerShell's pipeline buffer, eliminating the freeze.
$batContent = "@echo off`ncall `"$vc`"`n`"$cm`" --build `"$build`" --config Release --parallel --verbose"
$batFile = "$env:TEMP\aspectra_runner.bat"
Set-Content -Path $batFile -Value $batContent

$process = Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$batFile`"" -Wait -NoNewWindow -PassThru
if ($process.ExitCode -ne 0) {
    Write-Host "`nBuild failed with exit code $($process.ExitCode)" -ForegroundColor Red
    exit $process.ExitCode
}

Write-Host "`n[4/6] Inspecting dependencies..." -ForegroundColor Cyan
$source = "$build\Aspectra.exe"
$targetDir = 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra X'
$target = "$targetDir\Aspectra.exe"
$dump = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe'
$deploy = 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\work\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'

$deps = & $dump /dependents $source
if ($deps -match '(?im)^\s*(Qt6\S*d\.dll|MSVCRTD\.dll|ucrtbased\.dll)\s*$') {
    throw "Fatal: Debug dependency found in Release build!"
}
Write-Host "   -> Clean Release binaries verified." -ForegroundColor Green

Write-Host "`n[5/6] Deploying executable & verifying Qt runtime..." -ForegroundColor Cyan
if (!(Test-Path $targetDir)) { New-Item -ItemType Directory -Force -Path $targetDir | Out-Null }
Copy-Item -LiteralPath $source -Destination $target -Force

# Removed 'Out-Null' so windeployqt streams the exact DLLs and Qt plugins it syncs live
& $deploy --release --compiler-runtime --force --no-translations $target

Write-Host "`n[6/6] Launching Aspectra X & Synchronizing Git..." -ForegroundColor Cyan
# Explicit WorkingDirectory ensures Qt DLLs are located and prevents silent background crashes
Start-Process -FilePath $target -WorkingDirectory $targetDir
Write-Host "   -> Aspectra X launched asynchronously." -ForegroundColor Green

Write-Host "   -> Running live Git Sync..." -ForegroundColor DarkGray
Set-Location 'C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra'
git add --all
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    git commit -m "Auto-sync: $timestamp"
    # Runs standard push openly in the terminal so you can see the upload progress/speed
    git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=30 push origin main
} else {
    Write-Host "   -> Git is up to date, nothing to push." -ForegroundColor DarkGray
}

Write-Host "`nPipeline complete! App is live." -ForegroundColor Green