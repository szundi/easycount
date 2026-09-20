#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 variants/<variant>/package.json" >&2
  exit 2
fi

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
variant_manifest=$(realpath "$project_root/$1")

case "$variant_manifest" in
  "$project_root"/variants/*/package.json) ;;
  *)
    echo "Invalid variant manifest: $variant_manifest" >&2
    exit 2
    ;;
esac

app_name=$(node -e "const manifest = require(process.argv[1]); process.stdout.write(manifest.name);" "$variant_manifest")
temp_dir=$(mktemp -d "/tmp/easycount-${app_name}.XXXXXX")

cleanup() {
  if [[ -d "$temp_dir" && "$temp_dir" == /tmp/easycount-* ]]; then
    rm -rf -- "$temp_dir"
  fi
}
trap cleanup EXIT

cp "$variant_manifest" "$temp_dir/package.json"
cp "$project_root/wscript" "$temp_dir/wscript"
cp -R "$project_root/src" "$temp_dir/src"
cp -R "$project_root/resources" "$temp_dir/resources"

(
  cd "$temp_dir"
  pebble build
)

mkdir -p "$project_root/dist"
pbw_path=$(find "$temp_dir/build" -maxdepth 1 -type f -name '*.pbw' -print -quit)
if [[ -z "$pbw_path" ]]; then
  echo "Pebble build did not create a PBW file." >&2
  exit 1
fi
cp "$pbw_path" "$project_root/dist/${app_name}.pbw"
echo "Built dist/${app_name}.pbw"
