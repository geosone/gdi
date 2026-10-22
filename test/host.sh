#!/bin/sh
set -eu

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror \
	-Isrc src/hexpatch.c test/hexpatch_test.c -o "$work_dir/hexpatch_test"
"$work_dir/hexpatch_test" "$work_dir"
echo "hexpatch tests passed"

${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror \
	-Isrc src/regtxn.c test/regtxn_test.c -o "$work_dir/regtxn_test"
"$work_dir/regtxn_test"

${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror \
	-Isrc src/progman.c test/progman_test.c -o "$work_dir/progman_test"
mkdir "$work_dir/progman"
"$work_dir/progman_test" "$work_dir/progman"

# INSTALL /CHECK
${CC:-cc} -std=c89 -pedantic -Wall -Wextra -Werror -DGDI_CHECK_MAIN \
	-Isrc src/check.c -o "$work_dir/gdicheck"
mkdir "$work_dir/check"
printf '[Setup]\r\nTitel=x\r\nReadme=readme.txt\r\n[Options]\r\nOption=on|A\r\n[Find]\r\nCDEX=MSCDEX.EXE\r\n[Files]\r\n2|X.TXT\r\n?CDX|README.TXT\r\n[System.ini]\r\nAdd=386Enh|device=x.386\r\n[Autoexec.bat]\r\nAdd=1,?CDEX|rem ok\r\n[Ini]\r\nSett=a\r\n[Unbekannt]\r\n' \
	> "$work_dir/check/INSTALL.INI"
: > "$work_dir/check/README.TXT"
"$work_dir/gdicheck" "$work_dir/check/INSTALL.INI" > "$work_dir/check.out" && exit 1
printf '%s\n' "$work_dir/check/INSTALL.INI" \
	'line 2: unknown key Titel= in [Setup]' \
	'line 9: condition: there is no option 2' \
	'line 9: [Files] X.TXT is not next to the INI' \
	'line 10: condition ?CDX: not in [Find] or [Detect]' \
	'line 16: unknown key Sett= in [Ini]' \
	'line 17: unknown section [Unbekannt]' \
	'6 problems' | diff - "$work_dir/check.out"
echo "check tests passed"
