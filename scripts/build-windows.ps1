[CmdletBinding()]
param(
    [string]$QtDirectory = $env:QT_ROOT_DIR,
    [string]$CompilerDirectory,
    [string]$BuildDirectory,
    [ValidateRange(1, 64)][int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectDirectory 'build/windows' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
if (-not $QtDirectory) {
    $QtDirectory = Split-Path (Split-Path (Get-Command qmake.exe -ErrorAction Stop).Source -Parent) -Parent
}
$QtDirectory = (Resolve-Path $QtDirectory).Path
if (-not $CompilerDirectory) {
    $CompilerDirectory = Join-Path $QtDirectory '../../Tools/mingw1120_64/bin'
}
$CompilerDirectory = (Resolve-Path $CompilerDirectory).Path
$env:PATH = "$QtDirectory\bin;$CompilerDirectory;$env:PATH"
$qmake = Join-Path $QtDirectory 'bin/qmake.exe'
$make = Join-Path $CompilerDirectory 'mingw32-make.exe'

function Invoke-BuildCommand {
    param([string]$Command, [string[]]$Arguments)
    if (-not (Test-Path $Command -PathType Leaf)) { throw "Missing build tool: $Command" }
    # Windows PowerShell 5.1 wraps redirected native stderr as error records.
    # Compiler warnings must be judged by the native exit code.
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Command @Arguments
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousErrorAction }
    if ($exitCode -ne 0) { throw "$Command failed with exit code $exitCode" }
}

$qtVersion = & $qmake -query QT_VERSION
if ($LASTEXITCODE -ne 0 -or $qtVersion -ne '6.6.3') { throw 'Qt 6.6.3 is required.' }
$compilerVersion = & (Join-Path $CompilerDirectory 'g++.exe') -dumpfullversion
if ($LASTEXITCODE -ne 0 -or $compilerVersion -ne '11.2.0') { throw 'MinGW 11.2.0 is required.' }
Write-Host "Building Windows x64 with Qt $qtVersion / MinGW $compilerVersion"

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
Push-Location $BuildDirectory
try {
    Invoke-BuildCommand $qmake @('-r', (Join-Path $projectDirectory 'painttyWidget.pro'), '-spec', 'win32-g++', 'CONFIG+=release', 'CONFIG-=debug')
    Invoke-BuildCommand $make @("-j$Jobs")
} finally { Pop-Location }

$application = Join-Path $BuildDirectory 'build/MrPaint.exe'
if (-not (Test-Path $application -PathType Leaf)) { throw "Missing application: $application" }
$testDirectory = Join-Path $BuildDirectory 'local-tests'
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Push-Location $testDirectory
$oldPlatform = $env:QT_QPA_PLATFORM
try {
    Invoke-BuildCommand $qmake @((Join-Path $projectDirectory 'src/painttyDesktop/local-tests.pro'), '-spec', 'win32-g++', 'CONFIG+=release', 'CONFIG-=debug')
    Invoke-BuildCommand $make @("-j$Jobs")
    $env:QT_QPA_PLATFORM = 'offscreen'
    Invoke-BuildCommand (Join-Path $testDirectory 'bin/local-tests.exe') @(
        '-o', "$(Join-Path $BuildDirectory 'test-results.txt'),txt",
        '-o', "$(Join-Path $BuildDirectory 'test-results.xml'),junitxml",
        '-o', '-,txt'
    )
} finally {
    $env:QT_QPA_PLATFORM = $oldPlatform
    Pop-Location
}
Write-Host "Windows build and tests passed: $application"
