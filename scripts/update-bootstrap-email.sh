#!/bin/bash
# Refreshes third_party/bootstrap-email from the Ruby gem, on a host that has it (gem install bootstrap-email):
# the stylesheets compiled with its default settings, its templates, layout and version. Then make the fixtures'
# expected output with the gem too (tests/fixtures/*.expected.html: bootstrap-email X.html > X.expected.html).
#
#   scripts/update-bootstrap-email.sh [user@host]     (default: this machine)
set -euo pipefail

host="${1:-}"
dir=$(cd "$(dirname "$0")/.." && pwd)/third_party/bootstrap-email
run() { if [ -n "$host" ]; then ssh -o BatchMode=yes "$host" "$@"; else bash -c "$*"; fi; }

run 'set -e; work=$(mktemp -d); cd "$work"
gem_dir=$(dirname "$(dirname "$(gem which bootstrap-email)")")
ruby -e "require \"bootstrap-email\"; c = BootstrapEmail::Config.new({})
File.write(\"bootstrap-email.css\", BootstrapEmail::SassCache.compile(\"bootstrap-email\", c, style: :expanded))
File.write(\"bootstrap-head.css\", BootstrapEmail::SassCache.compile(\"bootstrap-head\", c))" > /dev/null
mkdir -p out/css out/templates
mv bootstrap-email.css bootstrap-head.css out/css/
cp "$gem_dir"/core/templates/*.html out/templates/
cp "$gem_dir"/core/layout.html.erb out/layout.html
cp "$gem_dir"/VERSION out/VERSION
tar -C out -cz . ; cd /; rm -rf "$work"' | tar -xz -C "$dir"
echo "Updated $dir to bootstrap-email $(cat "$dir/VERSION")"
