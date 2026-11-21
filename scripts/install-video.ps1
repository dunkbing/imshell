# installs imshell-video (render a scene.js to mp4, no compiler needed):
#   irm https://raw.githubusercontent.com/dunkbing/imshell/main/scripts/install-video.ps1 | iex
# $env:IMSHELL_VIDEO_VERSION = "video-vX.Y" picks a release (default: the latest), $env:IMSHELL_BIN the folder
$ErrorActionPreference = "Stop"
$repo = "dunkbing/imshell"
$bin = if ($env:IMSHELL_BIN) { $env:IMSHELL_BIN } else { Join-Path $env:LOCALAPPDATA "imshell-video" }
$file = "imshell-video-windows-x64.zip"
$base = if ($env:IMSHELL_VIDEO_VERSION) { "https://github.com/$repo/releases/download/$($env:IMSHELL_VIDEO_VERSION)" } else { "https://github.com/$repo/releases/latest/download" }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    Write-Host "downloading $file"
    Invoke-WebRequest "$base/$file" -OutFile "$tmp\$file"
    Invoke-WebRequest "$base/SHA256SUMS" -OutFile "$tmp\SHA256SUMS"
    $line = Get-Content "$tmp\SHA256SUMS" | Where-Object { $_ -match [regex]::Escape($file) + "$" } | Select-Object -First 1
    if (-not $line) { throw "no checksum for $file in SHA256SUMS" }
    $expected = ($line -split "\s+")[0]
    $actual = (Get-FileHash "$tmp\$file" -Algorithm SHA256).Hash.ToLower()
    if ($actual -ne $expected) { throw "checksum mismatch for $file" }
    Expand-Archive "$tmp\$file" -DestinationPath $tmp
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    Copy-Item "$tmp\imshell-video\imshell-video.exe" "$bin\imshell-video.exe" -Force
    Write-Host "installed $bin\imshell-video.exe"
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if (($userPath -split ";") -notcontains $bin) {
        [Environment]::SetEnvironmentVariable("Path", "$userPath;$bin", "User")
        Write-Host "added $bin to your PATH (open a new terminal)"
    }
    Write-Host "try: imshell-video api"
} finally {
    Remove-Item -Recurse -Force $tmp
}
