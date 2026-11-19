param([string]$Label = 'baseline')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Label must contain only letters, digits, _ or -.' }
$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repoRoot
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Write-Utf8($Path, $Lines) {
    [IO.File]::WriteAllLines($Path, [string[]]$Lines, $utf8)
}
function Git-Lines([string[]]$Arguments) {
    $result = & git -c core.quotepath=false @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git failed: $Arguments" }
    return $result
}
$captureRoot = Join-Path $repoRoot ('artifacts/baselines/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $Label)
if (Test-Path -LiteralPath $captureRoot) { throw 'Capture already exists.' }
New-Item -ItemType Directory -Path (Join-Path $captureRoot 'files') | Out-Null
Write-Utf8 (Join-Path $captureRoot 'head.txt') (Git-Lines @('rev-parse', 'HEAD'))
Write-Utf8 (Join-Path $captureRoot 'status.txt') (Git-Lines @('status', '--short', '--untracked-files=all'))
# Let git write patches directly, avoiding PowerShell redirection encoding changes.
Git-Lines @('diff', '--binary', ('--output=' + (Join-Path $captureRoot 'working.patch'))) | Out-Null
Git-Lines @('diff', '--cached', '--binary', ('--output=' + (Join-Path $captureRoot 'staged.patch'))) | Out-Null
$paths = @(Git-Lines @('ls-files', '--cached', '--others', '--exclude-standard'))
# OBJ files and NuGet dependencies are ignored by git, but required to reproduce a run/build.
foreach ($folder in @('src/assets', 'packages')) {
    $full = Join-Path $repoRoot $folder
    if (Test-Path -LiteralPath $full) {
        $paths += Get-ChildItem -LiteralPath $full -File -Recurse | ForEach-Object {
            $_.FullName.Substring($repoRoot.Length + 1).Replace('\', '/')
        }
    }
}
$manifest = foreach ($relative in ($paths | Sort-Object -Unique)) {
    $source = Join-Path $repoRoot $relative
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { continue }
    $target = Join-Path (Join-Path $captureRoot 'files') $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    $before = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $source -Destination $target
    $saved = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $after = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($before -ne $saved -or $after -ne $saved) { throw "File changed during capture: $relative" }
    [pscustomobject]@{ Path = $relative; SHA256 = $saved; Bytes = (Get-Item -LiteralPath $target).Length }
}
Write-Utf8 (Join-Path $captureRoot 'manifest.json') ($manifest | ConvertTo-Json -Depth 3)
Write-Utf8 (Join-Path $captureRoot 'COMPLETE.txt') @(
    'Capture completed; every copied file hash matched its source before and after copying.',
    'Restore files/ into a new empty directory; never overwrite the working repository.',
    'Deleted tracked paths are absent. Git history and build outputs are not included.',
    'Ignored inputs included: src/assets and packages. Other machine-local configuration is not included.'
)
Write-Output $captureRoot
