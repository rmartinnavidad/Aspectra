# =====================================================================
# ASPECTRA X - SILENT FILE WATCHER & AUTO-BUILD SCRIPT
# =====================================================================
$ErrorActionPreference = "Stop"

$watchPath = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra"
$targetFile = "MainWindow.cpp"
$buildScript = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra\build_and_deploy.ps1"

# Initialize FileSystemWatcher
$watcher = New-Object System.IO.FileSystemWatcher
$watcher.Path = $watchPath
$watcher.Filter = $targetFile
$watcher.IncludeSubdirectories = $false
$watcher.EnableRaisingEvents = $true

# Debounce timer to prevent multiple rapid triggers on a single save action
$lastTrigger = [DateTime]::MinValue
$debounceSeconds = 2

$action = {
    $changedFile = $Event.SourceEventArgs.Name
    if ($changedFile -eq $script:targetFile) {
        $now = [DateTime]::Now
        if (($now - $script:lastTrigger).TotalSeconds -ge $script:debounceSeconds) {
            $script:lastTrigger = $now
            
            # Execute the build & deploy pipeline completely hidden in the background
            Start-Process -FilePath "powershell.exe" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$script:buildScript`"" -WindowStyle Hidden
        }
    }
}

# Register the event listener
Register-ObjectEvent $watcher "Changed" -Action $action | Out-Null

# Keep the background watcher process alive indefinitely
while ($true) {
    Start-Sleep -Seconds 10
}