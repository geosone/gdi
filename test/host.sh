#!/bin/sh
set -eu

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror \
	-Isrc src/hexpatch.c test/hexpatch_test.c -o "$work_dir/hexpatch_test"
"$work_dir/hexpatch_test" "$work_dir"
echo "hexpatch tests passed"
