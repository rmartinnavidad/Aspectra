$ErrorActionPreference = "Continue"
$repoPath = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra"
Set-Location -LiteralPath $repoPath

Write-Host "Aspectra GitHub auto-sync is running..." -ForegroundColor Green
while ($true) {
    git fetch origin --prune

    # Respect .gitignore so deploy folders, runtimes, and Windows metadata
    # never become part of the source repository.
    git add --all
    git diff --cached --quiet
    if ($LASTEXITCODE -ne 0) {
        $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        git commit -m "Auto-sync: $timestamp"
    }

    # Reconcile any upstream work before publishing this local source change.
    git pull --rebase origin main
    if ($LASTEXITCODE -eq 0) {
        git push origin main
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Aspectra is synced: $(Get-Date -Format 'HH:mm:ss')" -ForegroundColor Green
        }
    } else {
        Write-Warning "GitHub sync paused for a rebase conflict; resolve it in the repository before the next sync."
        git rebase --abort
    }

    Start-Sleep -Seconds 60
}
