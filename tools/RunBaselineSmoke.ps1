param([Parameter(Mandatory=$true)][string]$RestoreRoot, [string]$Tag = 'smoke',
      [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
      [ValidateRange(5, 180)][int]$WaitSeconds = 60)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$resolved = (Resolve-Path -LiteralPath $RestoreRoot).Path
$allowed = [IO.Path]::GetFullPath((Join-Path $repoRoot 'artifacts/baselines')) + '\'
if (!$resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Expected an isolated baseline restore directory.' }
if ($Tag -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Invalid tag.' }
$logRoot = Join-Path $repoRoot 'artifacts/baselines'
$inputPath = Join-Path $logRoot ($Tag + '-input.txt')
$stdout = Join-Path $logRoot ($Tag + '-stdout.txt')
$stderr = Join-Path $logRoot ($Tag + '-stderr.txt')
foreach ($path in @($inputPath, $stdout, $stderr)) {
    if (Test-Path -LiteralPath $path) { throw "Output already exists: $path" }
}
[IO.File]::WriteAllText($inputPath, "0`r`n0`r`n", [Text.Encoding]::ASCII)
$process = Start-Process -FilePath (Join-Path $resolved ('x64/' + $Configuration + '/GPUMPMTex3D.exe')) -WorkingDirectory $resolved -WindowStyle Hidden -RedirectStandardInput $inputPath -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$null = $process.Handle
try {
    # Wait for loading and rendering, then request a normal window close.
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    while (!$process.WaitForExit(1000) -and [DateTime]::UtcNow -lt $deadline) { }
    if (!$process.HasExited) {
        $closeSent = $process.CloseMainWindow()
        Write-Output "CloseMainWindow sent: $closeSent"
        if (!$process.WaitForExit(10000)) { throw 'Application did not exit after close request.' }
    }
    $process.Refresh()
    Write-Output "ExitCode: $($process.ExitCode)"
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr
    if ($process.ExitCode -ne 0) { throw 'Application returned nonzero exit code.' }
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
