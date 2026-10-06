#!/usr/bin/env bash
# Deploy Qt, sign for integrity, and verify the extracted universal ZIP.
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    echo 'Usage: QTDIR=/path/to/Qt/6.6.3/macos ./scripts/package-macos.sh [v1.0.1|dev-local]'
    echo 'Optional overrides: PROJECT_DIR, BUILD_DIR, OUTPUT_DIR.'
    exit 0
fi
[[ $(uname -s) == Darwin ]] || { echo 'This script requires macOS.' >&2; exit 1; }
PROJECT_DIR=${PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
BUILD_DIR=${BUILD_DIR:-"$PROJECT_DIR/build/macos"}
OUTPUT_DIR=${OUTPUT_DIR:-"$PROJECT_DIR/dist"}
: "${QTDIR:?Set QTDIR to the Qt 6.6.3 macos directory.}"
VERSION_TAG=${1:-dev-local}
[[ $VERSION_TAG =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || { echo 'Invalid package version.' >&2; exit 1; }
APP_VERSION=$(tr -d '\r\n' < "$PROJECT_DIR/VERSION")
if [[ $VERSION_TAG == v* && ${VERSION_TAG#v} != "$APP_VERSION" ]]; then
    echo "Tag $VERSION_TAG does not match VERSION ($APP_VERSION)." >&2; exit 1
fi
SOURCE_COMMIT=$(git -C "$PROJECT_DIR" rev-parse HEAD)
PACKAGE_NAME="MrPaint-$VERSION_TAG-macos-universal"
mkdir -p "$OUTPUT_DIR" "$BUILD_DIR/package"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
STAGING_DIR="$BUILD_DIR/package/$PACKAGE_NAME"
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"
ditto "$BUILD_DIR/build/MrPaint.app" "$STAGING_DIR/MrPaint.app"
APP="$STAGING_DIR/MrPaint.app"
"$QTDIR/bin/macdeployqt" "$APP" -always-overwrite -verbose=2
mkdir -p "$APP/Contents/PlugIns/platforms"
ditto "$QTDIR/plugins/platforms/libqoffscreen.dylib" "$APP/Contents/PlugIns/platforms/libqoffscreen.dylib"
# Deploy dependencies of the explicitly included offscreen plugin too.
"$QTDIR/bin/macdeployqt" "$APP" -always-overwrite -verbose=2

RESOURCES="$APP/Contents/Resources"
mkdir -p "$RESOURCES/licenses"
cp "$PROJECT_DIR/src/painttyDesktop/LICENSE" "$RESOURCES/licenses/Paintty-LGPL-2.1.txt"
cp "$PROJECT_DIR/src/painttyDesktop/COPYING" "$RESOURCES/licenses/Paintty-COPYING.txt"
cp "$PROJECT_DIR/src/painttyDesktop/fonts/LICENSE_FOR_FONT" "$RESOURCES/licenses/Droid-font-Apache-2.0.txt"
cp "$PROJECT_DIR/packaging/windows/licenses/"*.txt "$RESOURCES/licenses/"
TOOLS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cp "$TOOLS_DIR/packaging/macos/README.txt" "$STAGING_DIR/README.txt"
python3 - "$APP" "$VERSION_TAG" "$APP_VERSION" "$SOURCE_COMMIT" <<'PY'
import json, pathlib, plistlib, re, subprocess, sys
app, version, application_version, commit = sys.argv[1:]
app = pathlib.Path(app)
plist_path = app / 'Contents/Info.plist'
with plist_path.open('rb') as f:
    info = plistlib.load(f)
info.update(CFBundleIdentifier='org.paintty.MrPaint', CFBundleDisplayName='Mr.Paint',
            CFBundleShortVersionString=application_version, CFBundleVersion=application_version,
            LSMinimumSystemVersion='11.0')
with plist_path.open('wb') as f:
    plistlib.dump(info, f)
metadata = dict(version=version, applicationVersion=application_version, commit=commit,
                qt='6.6.3', architecture='arm64+x86_64', minimumMacOS='11.0',
                signing='ad-hoc; not notarized',
                source=f'https://github.com/liuyanghejerry/painttyWidgetLegacy/tree/{commit}',
                qtSource='https://download.qt.io/archive/qt/6.6/6.6.3/single/')
(app / 'Contents/Resources/build-info.json').write_text(json.dumps(metadata, indent=2) + '\n')
# Every bundled Mach-O must support both CPUs and link only to bundled or system libraries.
magic = {bytes.fromhex(h) for h in ('cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca',
                                  'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe')}
for path in app.rglob('*'):
    if path.is_symlink() or not path.is_file():
        continue
    with path.open('rb') as f:
        if f.read(4) not in magic:
            continue
    subprocess.run(['lipo', str(path), '-verify_arch', 'x86_64', 'arm64'], check=True)
    loads = subprocess.check_output(['otool', '-l', str(path)], text=True)
    for rpath in set(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', loads)):
        if rpath.startswith('/') and not rpath.startswith(('/usr/lib/', '/System/Library/')):
            subprocess.run(['install_name_tool', '-delete_rpath', rpath, str(path)], check=True)
    links = subprocess.check_output(['otool', '-L', str(path)], text=True)
    for line in links.splitlines():
        if line.startswith('\t'):
            dependency = line.strip().split(' (', 1)[0]
            if dependency.startswith('/') and not dependency.startswith(('/usr/lib/', '/System/Library/')):
                raise SystemExit(f'External dependency: {path}: {dependency}')
PY
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
ARCHIVE="$OUTPUT_DIR/$PACKAGE_NAME.zip"
ditto -c -k --sequesterRsrc --keepParent "$STAGING_DIR" "$ARCHIVE"
SMOKE_DIR=$(mktemp -d "$BUILD_DIR/package-smoke.XXXXXX")
trap 'rm -rf "$SMOKE_DIR"' EXIT
ditto -x -k "$ARCHIVE" "$SMOKE_DIR"
EXTRACTED_APP="$SMOKE_DIR/$PACKAGE_NAME/MrPaint.app"
codesign --verify --deep --strict --verbose=2 "$EXTRACTED_APP"
mkdir -p "$SMOKE_DIR/home"
for cpu in arm64 x86_64; do
    for platform in cocoa offscreen; do
        env -i PATH=/usr/bin:/bin HOME="$SMOKE_DIR/home" TMPDIR="${TMPDIR:-/tmp}" \
            QT_QPA_PLATFORM="$platform" /usr/bin/arch -"$cpu" \
            "$EXTRACTED_APP/Contents/MacOS/MrPaint" --version \
            > "$BUILD_DIR/package-smoke-$cpu-$platform.txt"
        [[ $(cat "$BUILD_DIR/package-smoke-$cpu-$platform.txt") == "MrPaint $APP_VERSION" ]] || {
            echo "Packaged $cpu/$platform version check failed." >&2; exit 1;
        }
    done
done
cd "$OUTPUT_DIR"
shasum -a 256 "$PACKAGE_NAME.zip" > "$PACKAGE_NAME.zip.sha256"
shasum -a 256 --check "$PACKAGE_NAME.zip.sha256"
echo "Universal ZIP verified: $ARCHIVE"
if [[ -n ${GITHUB_OUTPUT:-} ]]; then
    echo "archive=$ARCHIVE" >> "$GITHUB_OUTPUT"
    echo "checksum=$ARCHIVE.sha256" >> "$GITHUB_OUTPUT"
fi
