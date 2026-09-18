$ErrorActionPreference = "Stop"
$repoPath = "C:\Users\rmart\Documents\Codex\2026-09-06\ki\outputs\Aspectra"
Set-Location -LiteralPath $repoPath
$env:GIT_TERMINAL_PROMPT = "0"

function Invoke-Git([string[]] $Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

try {
    # Mirror Aspectra's external icon/tool assets into this repository so GitHub
    # always contains the same files used by the local application.
    $toolsSource = "C:\Users\rmart\Reign of Glory\blender\00 Addons\custom add ons\utilities\ASPECTRA\tools"
    $toolsDestination = Join-Path $repoPath "tools"
    if (-not (Test-Path -LiteralPath $toolsSource -PathType Container)) {
        throw "Aspectra tools folder was not found: $toolsSource"
    }
    New-Item -ItemType Directory -Path $toolsDestination -Force | Out-Null
    & robocopy $toolsSource $toolsDestination /MIR /R:2 /W:1 /NFL /NDL /NJH /NJS /NP
    $robocopyExit = $LASTEXITCODE
    if ($robocopyExit -ge 8) { throw "robocopy tools mirror failed with exit code $robocopyExit" }

    # .tmp.driveupload is a Google Drive transport folder, excluded by .gitignore.
    Invoke-Git -Arguments @('add', '--all')
    & git diff --cached --quiet
    if ($LASTEXITCODE -eq 1) {
        Invoke-Git -Arguments @('commit', '-m', "Auto-sync: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
    } elseif ($LASTEXITCODE -ne 0) {
        throw "git diff failed with exit code $LASTEXITCODE"
    }
    # GitHub is the mirror. A non-fast-forward push fails visibly rather than
    # rebasing over independent upstream edits or scanning Drive's Git metadata.
    Invoke-Git -Arguments @('-c', 'http.lowSpeedLimit=1000', '-c', 'http.lowSpeedTime=30', 'push', 'origin', 'main')
    Write-Host "Aspectra is synced: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
} catch {
    Write-Error "Aspectra GitHub sync failed: $_"
    exit 1
}
