[CmdletBinding()]
param(
    [string]$QtDirectory = $env:QT_ROOT_DIR,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version = 'dev-local',
    [string]$Commit = $env:GITHUB_SHA
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path $PSScriptRoot -Parent
$applicationVersion = (Get-Content (Join-Path $projectDirectory 'VERSION') -Raw).Trim()
if ($Version.StartsWith('v') -and $Version.Substring(1) -ne $applicationVersion) {
    throw "Release tag $Version does not match application version $applicationVersion."
}
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectDirectory 'build/windows' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectDirectory 'dist' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $QtDirectory) {
    $QtDirectory = Split-Path (Split-Path (Get-Command qmake.exe -ErrorAction Stop).Source -Parent) -Parent
}
$QtDirectory = (Resolve-Path $QtDirectory).Path
$env:PATH = "$QtDirectory\bin;$(Join-Path $QtDirectory '../../Tools/mingw1120_64/bin');$env:PATH"
$application = Join-Path $BuildDirectory 'build/MrPaint.exe'
if (-not (Test-Path $application -PathType Leaf)) { throw "Build the application first: $application" }
if (-not $Commit) {
    $Commit = & git -C $projectDirectory rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit; supply -Commit.' }
}

$packageName = "MrPaint-$Version-windows-x64"
$stagingDirectory = Join-Path $BuildDirectory "package/$packageName"
if (Test-Path $stagingDirectory) { Remove-Item $stagingDirectory -Recurse -Force }
New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
$executable = Join-Path $stagingDirectory 'MrPaint.exe'
Copy-Item $application $executable
$previousErrorAction = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & (Join-Path $QtDirectory 'bin/windeployqt.exe') --release --compiler-runtime --no-translations $executable
    $deploymentExitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $previousErrorAction }
if ($deploymentExitCode -ne 0) { throw "windeployqt failed with exit code $deploymentExitCode" }

# Keep the startup check independent of the SDK's plugins and PATH.
Copy-Item (Join-Path $QtDirectory 'plugins/platforms/qoffscreen.dll') (Join-Path $stagingDirectory 'platforms/qoffscreen.dll')
"[Paths]`nPrefix=.`nPlugins=.`n" | Set-Content (Join-Path $stagingDirectory 'qt.conf') -Encoding ascii
foreach ($file in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll',
                    'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    if (-not (Test-Path (Join-Path $stagingDirectory $file) -PathType Leaf)) { throw "Missing runtime dependency: $file" }
}

$licenses = Join-Path $stagingDirectory 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item (Join-Path $projectDirectory 'src/painttyDesktop/LICENSE') (Join-Path $licenses 'Paintty-LGPL-2.1.txt')
Copy-Item (Join-Path $projectDirectory 'src/painttyDesktop/COPYING') (Join-Path $licenses 'Paintty-COPYING.txt')
Copy-Item (Join-Path $projectDirectory 'src/painttyDesktop/fonts/LICENSE_FOR_FONT') (Join-Path $licenses 'Droid-font-Apache-2.0.txt')
Copy-Item (Join-Path $projectDirectory 'packaging/windows/licenses/*') $licenses
Copy-Item (Join-Path $QtDirectory '../../Tools/mingw1120_64/licenses') (Join-Path $licenses 'MinGW') -Recurse
Copy-Item (Join-Path $projectDirectory 'packaging/windows/README.txt') (Join-Path $stagingDirectory 'README.txt')
$metadata = [ordered]@{
    version = $Version
    applicationVersion = $applicationVersion
    commit = $Commit
    qt = '6.6.3'
    compiler = 'MinGW 11.2.0'
    architecture = 'x64'
    source = "https://github.com/liuyanghejerry/painttyWidgetLegacy/tree/$Commit"
    qtSource = 'https://download.qt.io/archive/qt/6.6/6.6.3/single/'
}
$metadata | ConvertTo-Json | Set-Content (Join-Path $stagingDirectory 'build-info.json') -Encoding utf8

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$archive = Join-Path $OutputDirectory "$packageName.zip"
if (Test-Path $archive) { Remove-Item $archive -Force }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
# .NET Framework can use backslashes in ZIP names; normalize them for every reader.
$zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem $stagingDirectory -File -Recurse -Force) {
        $entryName = $file.FullName.Substring($stagingDirectory.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entryName, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $zip.Dispose() }

# Test the actual extracted ZIP with all Qt SDK paths removed.
$smokeDirectory = Join-Path $BuildDirectory "package-smoke/$packageName"
if (Test-Path $smokeDirectory) { Remove-Item $smokeDirectory -Recurse -Force }
[IO.Compression.ZipFile]::ExtractToDirectory($archive, $smokeDirectory)
$variables = @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH')
$previousEnvironment = @{}
foreach ($name in $variables) { $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $stdout = Join-Path $BuildDirectory 'package-smoke-stdout.txt'
    $stderr = Join-Path $BuildDirectory 'package-smoke-stderr.txt'
    $process = New-Object Diagnostics.Process
    $process.StartInfo.FileName = Join-Path $smokeDirectory 'MrPaint.exe'
    $process.StartInfo.Arguments = '--version'
    $process.StartInfo.WorkingDirectory = $smokeDirectory
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    if (-not $process.Start()) { throw 'Cannot start the packaged application.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw 'The packaged application did not exit within 15 seconds.'
    }
    $versionOutput = $stdoutTask.GetAwaiter().GetResult()
    $errorOutput = $stderrTask.GetAwaiter().GetResult()
    [IO.File]::WriteAllText($stdout, $versionOutput)
    [IO.File]::WriteAllText($stderr, $errorOutput)
    if ($process.ExitCode -ne 0) {
        throw "Packaged application failed ($($process.ExitCode)): $errorOutput"
    }
    if ($versionOutput.Trim() -ne "MrPaint $applicationVersion") { throw "Unexpected --version output: $versionOutput" }
} finally {
    if ($process) { $process.Dispose() }
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process') }
}

$hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256", "$hash  $packageName.zip`n", [Text.Encoding]::ASCII)
Write-Host "Portable ZIP verified: $archive"
if ($env:GITHUB_OUTPUT) {
    "archive=$archive" | Add-Content $env:GITHUB_OUTPUT -Encoding utf8
    "checksum=$archive.sha256" | Add-Content $env:GITHUB_OUTPUT -Encoding utf8
}
