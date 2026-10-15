#!/bin/sh
# Writes patches/ from the patched extern/gambatte-core tree: the reverse of
# apply-patches.sh. A patch is the tree's difference in a list of files. Edit
# the tree, run this, and apply-patches.sh must then say "already applied".
# A file is in ONE patch, whole.
set -eu
trap '' PIPE
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
tree="$root/extern/gambatte-core"

# name | files (space separated), in the order the patches apply
series='

'

# a patch that comes out the same is left as it is, so that a build is not
# taken for older than its patches (the test package:fresh)
keep="$(mktemp -d)"
printf '%s\n' "$series" | grep '|' | while IFS='|' read -r name files; do
	# shellcheck disable=SC2086
	git -C "$tree" diff -- $files > "$keep/$name.patch"
	cmp -s "$keep/$name.patch" "$root/patches/$name.patch" 2> /dev/null || cp "$keep/$name.patch" "$root/patches/$name.patch"
	echo "$name.patch: $(grep -c '^diff --git' "$root/patches/$name.patch") files"
done
# a patch the series no longer names goes
for p in "$root"/patches/*.patch; do
	[ -e "$keep/$(basename "$p")" ] || rm -f "$p"
done
rm -r "$keep"
# a changed file no patch names would be lost by a fresh clone
left="$(git -C "$tree" status --porcelain | wc -l)"
named="$(cat "$root"/patches/*.patch | grep -c '^diff --git')"
[ "$left" -eq "$named" ] || { echo "extern/gambatte-core has $left changed files and the patches name $named: add the rest to the series" >&2; exit 1; }
