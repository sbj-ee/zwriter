#!/usr/bin/env bash
# Fail if anything inside zwriter.app still links to (or searches) Homebrew or
# /usr/local. Checks every Mach-O file: main binary, frameworks, dylibs, plugins.
#   usage: tools/macos/check-bundle.sh path/to/zwriter.app
set -euo pipefail
app="${1:?usage: check-bundle.sh path/to/zwriter.app}"
[[ -x "$app/Contents/MacOS/zwriter" ]] || { echo "not an app bundle: $app" >&2; exit 2; }

bad='(/opt/homebrew|/usr/local)'
n=0; hits=0
while IFS= read -r -d '' f; do
  file -b "$f" | grep -q 'Mach-O' || continue
  n=$((n + 1))
  # Linked libraries (skip the first line, which is the file name itself).
  if otool -L "$f" | tail -n +2 | grep -E "$bad" >/dev/null; then
    echo "LINK  ${f#"$app"/}:"; otool -L "$f" | tail -n +2 | grep -E "$bad" | sed 's/^/        /'
    hits=$((hits + 1))
  fi
  # Run-path search entries.
  if otool -l "$f" | grep -A2 LC_RPATH | grep -E "path $bad" >/dev/null; then
    echo "RPATH ${f#"$app"/}:"; otool -l "$f" | grep -A2 LC_RPATH | grep -E "path $bad" | sed 's/^/        /'
    hits=$((hits + 1))
  fi
done < <(find "$app/Contents" -type f -print0)

echo "check-bundle: $n Mach-O files checked (main binary, frameworks, dylibs, plugins)"
echo "check-bundle: main binary links:"
otool -L "$app/Contents/MacOS/zwriter" | tail -n +2 | sed 's/^/    /'
echo "check-bundle: plugins: $(find "$app/Contents/PlugIns" -name '*.dylib' | wc -l | tr -d ' ')," \
     "frameworks: $(find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' | wc -l | tr -d ' ')," \
     "dylibs: $(find "$app/Contents/Frameworks" -maxdepth 1 -name '*.dylib' | wc -l | tr -d ' ')"
for need in Contents/Resources/zwriter.icns Contents/Resources/hunspell/en_US.aff \
            Contents/Resources/hunspell/en_US.dic Contents/Resources/qt.conf \
            Contents/PlugIns/platforms/libqcocoa.dylib; do
  [[ -e "$app/$need" ]] || { echo "MISSING $need"; hits=$((hits + 1)); }
done
if (( hits > 0 )); then
  echo "check-bundle: FAILED ($hits problem(s))" >&2
  exit 1
fi
echo "check-bundle: OK, no /opt/homebrew or /usr/local references"
