# =====================================================================
# ASPECTRA X - FILE WATCHER & AUTO-BUILD PIPELINE
# =====================================================================
$watcher = New-Object System.IO.FileSystemWatcher
$watcher.Path = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra"
$watcher.Filter = "MainWindow.cpp"
$watcher.IncludeSubdirectories = $false
$watcher.EnableRaisingEvents = $true

Write-Host "Watching 'MainWindow.cpp' for changes... Saving will auto-trigger build." -ForegroundColor Green

while ($true) {
    $event = Wait-Event -Timeout 1
    # Check for file changes (Changed or Created event)
    $changes = Get-Event -SourceIdentifier "System.IO.FileSystemWatcher" -ErrorAction SilentlyContinue
    if ($changes) {
        Unregister-Event -SourceIdentifier "System.IO.FileSystemWatcher" -ErrorAction SilentlyContinue
        Write-Host "`n[Change Detected] MainWindow.cpp saved. Running build pipeline..." -ForegroundColor Yellow
        
        # Run your master build and deploy script
        & "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra\build_and_deploy.ps1"
        
        Write-Host "Ready for next change..." -ForegroundColor Green
    }
    
    # Re-register the event trigger
    Register-ObjectEvent $watcher "Changed" -Action { } | Out-Null
}