# Build script for nwinfo
$vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"

if (Test-Path $vsPath) {
    # Run VsDevCmd.bat to set up the build environment
    & "$vsPath"
    
    # Build the solution
    msbuild nwinfo.sln /p:Configuration=Release
} else {
    Write-Host "Visual Studio 2022 not found at $vsPath"
    exit 1
}
