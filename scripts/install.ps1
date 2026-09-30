# Downloads the latest successful CI build of Transcriber and installs it for Ableton Live.
# Run from an administrator PowerShell (the VST3 folder is under Program Files):
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1
# Optional: -RunId <id> to install a specific build instead of the latest.

param([string]$RunId)

$ErrorActionPreference = "Stop"
$repo = "mtgmartin/transcriber"
$target = "C:\Program Files\Common Files\VST3"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { throw "Run this script from an administrator PowerShell; the VST3 folder needs admin rights." }

if (Get-Process | Where-Object { $_.ProcessName -like "Ableton Live*" }) {
    throw "Close Ableton Live first; it locks the plugin file while running."
}

if (-not $RunId) {
    $RunId = gh run list --repo $repo --workflow build.yml --status success --limit 1 --json databaseId --jq ".[0].databaseId"
    if (-not $RunId) { throw "No successful build found for $repo." }
}

$download = Join-Path $env:TEMP "transcriber-$RunId"
if (Test-Path $download) { Remove-Item -Recurse -Force $download }

Write-Host "Downloading build $RunId..."
gh run download $RunId --repo $repo --name Transcriber-VST3 --dir $download
if ($LASTEXITCODE -ne 0) { throw "Download failed." }

$bundle = Join-Path $download "Transcriber.vst3"
if (-not (Test-Path $bundle)) { throw "Transcriber.vst3 not found in the downloaded build." }

$installed = Join-Path $target "Transcriber.vst3"
if (Test-Path $installed) { Remove-Item -Recurse -Force $installed }
Copy-Item -Recurse $bundle $target

Remove-Item -Recurse -Force $download
Write-Host "Installed build $RunId to $installed"
