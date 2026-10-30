#!/bin/sh
# Crée une release GitHub : ./release/make-release.sh 0.2.0
# Produit 3 zips faciles à utiliser (app Mac Apple Silicon / Intel, exe Windows) et les publie avec release/NOTES.md.
set -e
V="$1"; [ -n "$V" ] || { echo "usage: $0 <version>  (ex: 0.2.0)"; exit 1; }
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
./build.sh >/dev/null
OUT="$(mktemp -d)"
mk_app() { # $1 binaire dans dist/, $2 nom du zip
  A="$OUT/StageWatch.app"; rm -rf "$A"; mkdir -p "$A/Contents/MacOS"
  cp "dist/$1" "$A/Contents/MacOS/stagewatch-bin"
  cat > "$A/Contents/MacOS/StageWatch" <<'SH'
#!/bin/sh
# Ouvre StageWatch dans une fenêtre Terminal (adresse visible, fermer = arrêter).
# Noms, photos et journaux : ~/Documents/StageWatch
DIR="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/Documents/StageWatch"
cp -f "$DIR/stagewatch-bin" "$HOME/Documents/StageWatch/.stagewatch" && chmod +x "$HOME/Documents/StageWatch/.stagewatch"
open -a Terminal "$HOME/Documents/StageWatch/.stagewatch"
SH
  chmod +x "$A/Contents/MacOS/StageWatch" "$A/Contents/MacOS/stagewatch-bin"
  cat > "$A/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>StageWatch</string>
<key>CFBundleExecutable</key><string>StageWatch</string>
<key>CFBundleIdentifier</key><string>com.alainicolas.stagewatch</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$V</string>
</dict></plist>
PL
  (cd "$OUT" && ditto -c -k --keepParent StageWatch.app "$2" && rm -rf StageWatch.app)
}
mk_app StageWatch-mac-arm64 StageWatch-Mac-Apple-Silicon.zip
mk_app StageWatch-mac-intel StageWatch-Mac-Intel.zip
(cd dist && zip -q "$OUT/StageWatch-Windows.zip" StageWatch.exe)
git tag -a "v$V" -m "StageWatch v$V"
git push origin "v$V"
gh release create "v$V" "$OUT"/*.zip --title "StageWatch v$V" --notes-file release/NOTES.md
