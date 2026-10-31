param([int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    & cmake --preset windows-x64 2>&1 | Tee-Object -FilePath logs/configure.log
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
    & cmake --build --preset windows-x64 --parallel $Jobs 2>&1 | Tee-Object -FilePath logs/build.log
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    Copy-Item -LiteralPath (Join-Path $root 'LICENSE'),(Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $root 'bin') -Force
    Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination (Join-Path $root 'bin') -Recurse -Force
} finally { Pop-Location }
