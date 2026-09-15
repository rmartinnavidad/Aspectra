# Set the path to your Aspectra root folder
$repoPath = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra"
cd $repoPath

Write-Host "Aspectra Auto-Sync is running..." -ForegroundColor Green

while ($true) {
    # Check if there are any changes
    $status = git status --porcelain
    
    if ($status) {
        Write-Host "Changes detected. Syncing to GitHub..." -ForegroundColor Yellow
        
        # Add, commit, and push
        git add .
        $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        git commit -m "Auto-sync: $timestamp"
        git push origin main
        
        Write-Host "Sync complete!" -ForegroundColor Green
    }
    
    # Wait 60 seconds before checking again
    Start-Sleep -Seconds 60
}