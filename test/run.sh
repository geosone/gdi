#!/bin/bash
# Test INSTALL.EXE in QEMU: boots DOS (floppy image), runs INSTALL with
# test/TEST.INI (or INI=...), keys via the QEMU monitor (KEYS), shows the
# changed CONFIG.SYS/AUTOEXEC.BAT and the target directory on COM1.
#   test/run.sh            install
#   UNINST=1 test/run.sh   install, then INSTALL /U from the target directory
#   PKG=dir TARGET=NAME test/run.sh   install a distribution directory
#                       (all files of dir, its INSTALL.INI) into A:\NAME
#   PKG=test/combined ONEEXE=1 TARGET=PKGA KEYS=... test/run.sh   [Installer]
#   PKG=test/depends ONEEXE=1 DEPENDS=1 test/run.sh   Requires=, Conflicts=
#   INI=test/TESTINI.INI WINDOWS=1 INIFILES=1 TARGET=TESTINI KEYS="ret ret ret ret ret"
#       [UNINST=1] test/run.sh   [Ini], [System.ini] Change=
#   PKG=test/newer TARGET=NEWTEST NEWER=1 KEYS="ret ret ret ret" [UNINST=1] test/run.sh
#                       [Newer]
#   PKG=test/owner ONEEXE=1 OWNER=1 KEYS=... test/run.sh   a file replaced by
#                       two packages, the first one removed first
#   SILENT=1 TARGET=SILENT test/run.sh   INSTALL /S:answers, no keys
#   PKG=test/depends ONEEXE=1 MANAGE=1 TARGET=PKGA TIMEOUT=15 KEYS=... test/run.sh
#                       then INSTALL /M removes both packages
#   CHECK=1 test/run.sh   INSTALL /CHECK of test/TEST.INI
#   PKG=test/extras ONEEXE=1 EXTRAS=1 TARGET=MAIN TIMEOUT=15 KEYS=... test/run.sh
#                       [Extras]: A installed from its directory, B skipped
#                       (EXTRAS=none: A unticked in the list, B skipped)
#   PKG=test/setupdir ONEEXE=1 SETUPDIR=1 TARGET=SHARED TIMEOUT=15 KEYS=... test/run.sh
#                       two packages in A:\SHARED, each with its own setup
#                       directory: A removed with /U, then B with /M
# Environment: BOOTIMG (DOS boot floppy), INI, KEYS, TIMEOUT
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/test
BOOTIMG=${BOOTIMG:-$HOME/Iso/Dos/dos71de/Dos7.1_System_deutsch.IMG}
INI=${INI:-$ROOT/test/TEST.INI}
rm -rf "$OUT"; mkdir -p "$OUT/src"
cp "$BOOTIMG" "$OUT/boot.img"; chmod u+w "$OUT/boot.img"
crlf() { sed 's/$/\r/'; }
TARGET=${TARGET:-TESTDRV}
ARGS=${ARGS:-}
[ -n "${SILENT:-}" ] && ARGS="/S:A:\SRC\ANS.INI $ARGS"
if [ -n "${PKG:-}" ]; then
    cp -r "$PKG"/* "$OUT/src/"
    # the installer next to every INSTALL.INI of the distribution
    # (ONEEXE=1: only next to the top one, the floppy is small)
    find "$OUT/src" -name INSTALL.INI | while read -r i; do
        [ -n "${ONEEXE:-}" ] && [ "$(dirname "$i")" != "$OUT/src" ] && continue
        [ -f "$(dirname "$i")/INSTALL.EXE" ] || cp "$ROOT/build/INSTALL.EXE" "$(dirname "$i")/"
    done
else
    cp "$ROOT/build/INSTALL.EXE" "$OUT/src/"
    cp "$INI" "$OUT/src/INSTALL.INI"
    cp "$ROOT/LICENSE" "$OUT/src/LICENSE.TXT"
    printf 'Second component license\nMust also be accepted.\n' | crlf > "$OUT/src/LICENSE2.TXT"
    printf 'TESTDRV readme\nline 2\n' | crlf > "$OUT/src/README.TXT"
    printf 'TESTDRV test driver\r\n' > "$OUT/src/TESTDRV.SYS"
    printf '\xcd\x20' > "$OUT/src/TESTTOOL.COM"      # INT 20h
    printf '\xb8\x00\x4c\xcd\x21' > "$OUT/src/YES.COM"
    printf '\xb8\x01\x4c\xcd\x21' > "$OUT/src/NO.COM"
    printf '[Language]\r\nSelected=en\r\n[Keep]\r\nValue=preserved\r\n' > "$OUT/src/SETTINGS.INI"
    printf 'TESTVXD test VxD\r\n' > "$OUT/src/TESTVXD.386"
fi
[ -n "${SILENT:-}" ] && printf '[Answers]\r\nDir=A:\\SILENT\r\n' > "$OUT/src/ANS.INI"
printf 'org 100h\nmov dx,501h\nout dx,al\nint 20h\n' > "$OUT/qexit.asm" && nasm -f bin -o "$OUT/src/QEXIT.COM" "$OUT/qexit.asm"
if [ -n "${MENU:-}" ]; then   # multi-config CONFIG.SYS
    { echo '[menu]'; echo 'menuitem=STD,Standard'; echo 'menudefault=STD,0'; echo '[STD]'; echo 'DEVICE=A:\HIMEM.SYS'; echo '[common]'; echo 'FILES=30'; } | crlf > "$OUT/CONFIG.SYS"
else
    { echo 'DEVICE=A:\HIMEM.SYS'; echo 'DOS=HIGH'; echo 'FILES=30'; } | crlf > "$OUT/CONFIG.SYS"
fi
# the steps run in RUN.BAT: INSTALL changes AUTOEXEC.BAT while it runs
{
    echo '@ECHO OFF'
    if [ -n "${CHECK:-}" ]; then
        echo 'A:\SRC\INSTALL.EXE /CHECK > AUX'
    else
        echo "A:\\SRC\\INSTALL.EXE $ARGS"
    fi
    if [ -n "${SETUPDIR:-}" ]; then   # A with /U, then B with /M
        echo 'ECHO === INSTALLED > AUX'; echo 'DIR /S /B A:\SHARED > AUX'; echo 'TYPE A:\GDI.INI > AUX'
        echo 'TYPE A:\CONFIG.SYS > AUX'; echo 'TYPE A:\AUTOEXEC.BAT > AUX'
        echo 'A:\SRC\INSTALL.EXE /U A:\SHARED\SDA\INSTALL.INI'
        echo 'ECHO === AFTER A > AUX'; echo 'DIR /S /B A:\SHARED > AUX'; echo 'TYPE A:\SHARED\COMMON.SYS > AUX'; echo 'TYPE A:\CONFIG.SYS > AUX'
        echo 'TYPE A:\AUTOEXEC.BAT > AUX'
        echo 'A:\SRC\INSTALL.EXE /M'
        echo 'ECHO === AFTER B > AUX'; echo 'DIR A:\SHARED > AUX'; echo 'TYPE A:\GDI.INI > AUX'
    fi
    [ -n "${MANAGE:-}" ] && echo 'A:\SRC\INSTALL.EXE /M'
    [ -n "${MANAGE:-}" ] && { echo 'ECHO === GDI.INI > AUX'; echo 'TYPE A:\GDI.INI > AUX'; }
    [ -n "${UNINST:-}" ] && echo "A:\\$TARGET\\INSTALL.EXE /U"
    if [ -n "${OWNER:-}" ]; then   # remove X first, then Y
        echo 'A:\SRC\INSTALL.EXE /U A:\PKGX\INSTALL.INI'
        echo 'ECHO === AFTER X > AUX'; echo 'TYPE A:\SHARED.TXT > AUX'; echo 'TYPE A:\NEW.TXT > AUX'
        echo 'A:\SRC\INSTALL.EXE /U A:\PKGY\INSTALL.INI'
        echo 'ECHO === AFTER Y > AUX'; echo 'TYPE A:\SHARED.TXT > AUX'; echo 'DIR /B A:\NEW.* A:\SHARED.* > AUX'
    fi
    echo 'ECHO === CONFIG.SYS > AUX'; echo 'TYPE A:\CONFIG.SYS > AUX'
    echo 'ECHO === AUTOEXEC.BAT > AUX'; echo 'TYPE A:\AUTOEXEC.BAT > AUX'
    echo 'ECHO === TARGET > AUX'; echo "DIR A:\\$TARGET > AUX"
    echo 'ECHO === BACKUPS > AUX'; echo 'DIR A:\*.G0? > AUX'
    echo 'A:\SRC\QEXIT'
} | crlf > "$OUT/src/RUN.BAT"
{ echo '@ECHO OFF'
  # SetupDir: PathAppend= and PathRemove= change this line
  [ -n "${SETUPDIR:-}" ] && echo 'PATH A:\DOS;A:\OLDA;A:\TOOLS'
  echo 'A:\SRC\RUN.BAT'; } | crlf > "$OUT/AUTOEXEC.BAT"
P="$OUT/boot.img"
mmd -i "$P" ::/SRC 2>/dev/null
mcopy -s -o -i "$P" "$OUT"/src/* ::/SRC/ && mcopy -o -i "$P" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/ || exit 1
if [ -n "${NEWER:-}" ]; then   # version 2.0 of each DLL is there already
    for f in KEEP TAKE ARCKEEP ARCTAKE; do mcopy -o -i "$P" "$ROOT/test/newer/OLD2.DLL" "::/$f.DLL"; done
fi
[ -n "${OWNER:-}" ] && { printf 'original\r\n' > "$OUT/SHARED.TXT"; mcopy -o -i "$P" "$OUT/SHARED.TXT" ::/; }
if [ -n "${WINDOWS:-}" ]; then   # a fake Windows directory with SYSTEM.INI
    mmd -i "$P" ::/WINDOWS
    printf '\xcd\x20' > "$OUT/WIN.COM"
    printf '[boot]\r\nshell=progman.exe\r\n\r\n[386Enh]\r\ndevice=*vpicd\r\n\r\n[NonWindowsApp]\r\n' > "$OUT/SYSTEM.INI"
    mcopy -o -i "$P" "$OUT/WIN.COM" "$OUT/SYSTEM.INI" ::/WINDOWS/
    sed -i 's/^A:\\SRC\\QEXIT/ECHO === SYSTEM.INI > AUX\r\nTYPE A:\\WINDOWS\\SYSTEM.INI > AUX\r\nA:\\SRC\\QEXIT/' "$OUT/src/RUN.BAT"
    mcopy -o -i "$P" "$OUT/src/RUN.BAT" ::/SRC/
fi
if [ -n "${INIFILES:-}" ]; then   # CONTROL.INI for [Ini] Set=
    printf '[MMCPL]\r\nold=1\r\nkframe=old\r\n\r\n[other]\r\nx=1\r\n' > "$OUT/CONTROL.INI"
    mcopy -o -i "$P" "$OUT/CONTROL.INI" ::/WINDOWS/
fi
if [ -n "${HEXPATCH:-}" ]; then
    mmd -i "$P" ::/WINDOWS/SYSTEM
    printf '\x10\x66\xC7\x46\x49\xFF\xFF\x20\x66\xC7\x46\x49\xFF\xFF\x30' > "$OUT/WIN386.EXE"
    printf '\x10\xB9\x00\x00\x10\x00\x90\xE2\xFE\xCD\x20\x3F\x00\x01\x00\x2B\xC2\xB9\xE8\x03\x00\x00\xF7\xE1\xA3\x20' > "$OUT/NDIS.386"
    mcopy -o -i "$P" "$OUT/WIN386.EXE" "$OUT/NDIS.386" ::/WINDOWS/SYSTEM/
fi
DEFAULT_KEYS="ret ret ret ret ret ret ret ret ret"
[ -n "${UNINST:-}" ] && DEFAULT_KEYS="ret ret ret ret ret ret"
[ -n "${SILENT:-}${CHECK:-}" ] && DEFAULT_KEYS="shift"   # no questions
KEYS=${KEYS:-$DEFAULT_KEYS}
[ -n "${UNINST:-}" ] && KEYS="$KEYS ${UNKEYS:-r}"
{
    sleep ${START:-8}; n=0
    for k in $KEYS; do n=$((n+1)); echo "screendump $OUT/s$n.ppm"; sleep 1; echo "sendkey $k"; sleep 2; done
    sleep ${TIMEOUT:-10}; echo "screendump $OUT/end.ppm"; sleep 1; echo quit
} | qemu-system-i386 -machine pc -accel kvm -m 16 -display none -vga std -monitor stdio \
    -drive if=floppy,format=raw,file="$P" -boot a -nic none -serial file:"$OUT/serial.log" \
    -device isa-debug-exit,iobase=0x501,iosize=0x02 > "$OUT/monitor.log" 2>&1
for f in "$OUT"/*.ppm; do [ -f "$f" ] && magick "$f" "${f%.ppm}.png" && rm -f "$f"; done
tr -d '\r' < "$OUT/serial.log"
if [ -n "${LANGUAGE:-}" ]; then
    mcopy -o -i "$P" ::/TESTLANG/SETTINGS.INI "$OUT/LANGUAGE.INI"
    python3 -c 'import configparser,sys; c=configparser.ConfigParser(); c.read(sys.argv[1]); assert c["Language"]["Selected"] == "de"; assert c["Keep"]["Value"] == "preserved"' "$OUT/LANGUAGE.INI" || exit 1
    echo "DOS language selection integration passed"
fi
if [ -n "${DEPENDS:-}" ]; then
    # B is required by A: selected as well and installed first; C conflicts
    mcopy -o -i "$P" ::/GDI.INI "$OUT/GDI.INI"
    python3 -c 'import sys; k=[l.split("=")[0] for l in open(sys.argv[1]).read().splitlines() if "=" in l]; assert k == ["GDI-B", "Package A"], k' "$OUT/GDI.INI" || exit 1
    ! mdir -i "$P" ::/PKGC >/dev/null 2>&1 || exit 1
    echo "DOS Requires/Conflicts integration passed"
fi
if [ -n "${MANAGE:-}" ]; then
    # INSTALL /M, both checked: GDI.INI empty, the directories gone
    tr -d '\r' < "$OUT/serial.log" | sed -n '/=== GDI.INI/,/=== CONFIG.SYS/p' | grep -v '^===' | grep -q '=' && { echo "GDI.INI not empty"; exit 1; }
    for d in PKGA PKGB; do mdir -i "$P" ::/$d >/dev/null 2>&1 && { echo "$d left"; exit 1; }; done
    echo "DOS INSTALL /M integration passed"
fi
if [ -n "${EXTRAS:-}" ]; then
    grep -aq '=== TARGET' "$OUT/serial.log" || { echo "setup did not end"; exit 1; }
    mcopy -o -i "$P" ::/GDI.INI "$OUT/GDI.INI" 2>/dev/null || : > "$OUT/GDI.INI"
    if [ "$EXTRAS" = none ]; then   # A unticked, B skipped
        [ -s "$OUT/GDI.INI" ] && { echo "extras installed"; exit 1; }
    else
        tr -d '\r' < "$OUT/GDI.INI" | grep -q '^SDA=A:\\SHARED\\SDA|' || { echo "A not installed"; exit 1; }
    fi
    tr -d '\r' < "$OUT/GDI.INI" | grep -q '^SD[BC]=' && { echo "B or C installed"; exit 1; }
    mdir -i "$P" ::/MAIN/MAIN.TXT >/dev/null 2>&1 || { echo "main package missing"; exit 1; }
    mdir -i "$P" ::/SHARED/DRVB.SYS >/dev/null 2>&1 && { echo "B installed"; exit 1; }
    # PathAppend= of A without a PATH line: a line of its own, the PATH of
    # the system stays
    mcopy -o -i "$P" ::/AUTOEXEC.BAT "$OUT/AUTOEXEC.NEW" || exit 1
    if [ "$EXTRAS" = none ]; then
        grep -qi '^PATH' "$OUT/AUTOEXEC.NEW" && { echo "PATH added"; exit 1; }
    else
        tr -d '\r' < "$OUT/AUTOEXEC.NEW" | grep -qx 'PATH %PATH%;A:\\SHARED' || { echo "PATH line missing"; cat "$OUT/AUTOEXEC.NEW"; exit 1; }
    fi
    echo "DOS [Extras] integration passed"
fi
if [ -n "${SETUPDIR:-}" ]; then
    tr -d '\r' < "$OUT/serial.log" > "$OUT/setupdir.txt"
    python3 - "$OUT/setupdir.txt" <<'PYEOF' || exit 1
import sys
t = open(sys.argv[1], errors="replace").read()
inst, rest = t.split("=== AFTER A")[0].split("=== INSTALLED")[1], t.split("=== AFTER A")[1]
a, b = rest.split("=== AFTER B")
b = b.split("=== CONFIG.SYS")[0]
up = inst.upper()
for f in ("A:\\SHARED\\DRVA.SYS", "A:\\SHARED\\DRVB.SYS", "A:\\SHARED\\COMMON.SYS",
          "A:\\SHARED\\SDA\\INSTALL.INI", "A:\\SHARED\\SDA\\INSTALL.LOG",
          "A:\\SHARED\\SDB\\INSTALL.INI", "SDA=A:\\SHARED\\SDA|", "SDB=A:\\SHARED\\SDB|"):
    assert f in up, ("installed", f)
ua = a.upper()
assert "DRVA.SYS" not in ua and "\\SDA" not in ua, ("A left", a)
assert "A:\\SHARED\\DRVB.SYS" in ua and "COMMON FROM B" in ua and "\\SDB\\INSTALL.INI" in ua, ("B gone", a)
assert "DEVICE=A:\\SHARED\\DRVB.SYS" in ua and "DRVA.SYS" not in ua, a
assert "SDB=" not in b.upper() and "SDA=" not in b.upper(), ("GDI.INI", b)
# [Find] of B: DRVA.SYS of A in the shared directory
assert "REM FOUND A:\\SHARED\\DRVA.SYS" in up, ("find in %DIR%", inst)
# PathAppend=: once, while one of the packages is there; PathRemove=: only
# its part of the line
import re
def paths(s):
    return re.findall(r"^PATH (.*)$", s, re.M | re.I)
assert paths(inst) == ["A:\\DOS;A:\\TOOLS;A:\\SHARED"], ("PATH installed", paths(inst))
assert paths(a) == ["A:\\DOS;A:\\TOOLS;A:\\SHARED"], ("PATH after A", paths(a))
final = t.split("=== AUTOEXEC.BAT")[-1].split("=== TARGET")[0]
assert paths(final) == ["A:\\DOS;A:\\TOOLS"], ("PATH after B", paths(final))
PYEOF
    mdir -i "$P" ::/SHARED >/dev/null 2>&1 && { echo "A:\SHARED left"; exit 1; }
    echo "DOS SetupDir integration passed"
fi
if [ -n "${SILENT:-}" ]; then
    mcopy -o -i "$P" ::/SILENT/TESTDRV.SYS "$OUT/SILENT.SYS" || exit 1
    tr -d '\r' < "$OUT/serial.log" | sed -n '/=== CONFIG.SYS/,/=== AUTOEXEC/p' | grep -qi 'SILENT\\TESTDRV.SYS' || exit 1
    echo "DOS INSTALL /S integration passed"
fi
if [ -n "${CHECK:-}" ]; then
    tr -d '\r' < "$OUT/serial.log" | grep -qx 'no problems' || exit 1
    echo "DOS INSTALL /CHECK integration passed"
fi
if [ -n "${INIFILES:-}" ]; then
    mcopy -o -i "$P" ::/WINDOWS/CONTROL.INI "$OUT/CONTROL-AFTER.INI"
    mcopy -o -i "$P" ::/WINDOWS/SYSTEM.INI "$OUT/SYSTEM-AFTER.INI"
    if [ -n "${UNINST:-}" ]; then
        cmp "$OUT/CONTROL.INI" "$OUT/CONTROL-AFTER.INI" || exit 1
        cmp "$OUT/SYSTEM.INI" "$OUT/SYSTEM-AFTER.INI" || exit 1   # Change= undone
        ! mdir -i "$P" ::/APP.INI >/dev/null 2>&1 || exit 1
    else
        mcopy -o -i "$P" ::/APP.INI "$OUT/APP.INI" || exit 1
        tr -d '\r' < "$OUT/SYSTEM-AFTER.INI" | grep -qx 'device=A:\\TESTINI\\VPICDA.386' || exit 1
        ! tr -d '\r' < "$OUT/SYSTEM-AFTER.INI" | grep -qx 'device=\*vpicd' || exit 1
        python3 -c 'import configparser,sys; c=configparser.ConfigParser(); c.read(sys.argv[1]); assert c["MMCPL"]["kframe"].upper() == "A:\\TESTINI\\KFRAME.CPL"; assert c["MMCPL"]["old"] == "1"; assert c["drivers.desc"]["test.drv"] == "Test driver"; a=configparser.ConfigParser(); a.read(sys.argv[2]); assert a["Options"]["Mode"] == "2"' "$OUT/CONTROL-AFTER.INI" "$OUT/APP.INI" || exit 1
    fi
    echo "DOS [Ini] Set integration passed"
fi
if [ -n "${NEWER:-}" ]; then
    # 1.0 does not replace 2.0, 3.0 does; INSTALL /U puts 2.0 back
    for f in KEEP TAKE ARCKEEP ARCTAKE; do mcopy -o -i "$P" "::/$f.DLL" "$OUT/$f.DLL"; done
    want3=3; [ -n "${UNINST:-}" ] && want3=2
    for f in KEEP ARCKEEP; do grep -q 'version 2\.0' "$OUT/$f.DLL" || { echo "$f.DLL replaced"; exit 1; }; done
    for f in TAKE ARCTAKE; do grep -q "version $want3\\.0" "$OUT/$f.DLL" || { echo "$f.DLL: not version $want3"; exit 1; }; done
    ! mdir -b -i "$P" ::/NEWTEST 2>/dev/null | grep -i GDINEW || exit 1
    echo "DOS [Newer] integration passed"
fi
if [ -n "${OWNER:-}" ]; then
    # X removed first: Y's files stay; Y removed: the original is back
    tr -d '\r' < "$OUT/serial.log" | sed -n '/=== AFTER X/,/=== CONFIG.SYS/p' > "$OUT/owner.txt"
    python3 -c 'import sys; t=open(sys.argv[1]).read().split("=== AFTER Y"); assert "shared from Y" in t[0] and "new from Y" in t[0], t; assert "original" in t[1], t' "$OUT/owner.txt" || exit 1
    ! mdir -b -i "$P" ::/ | grep -i 'NEW\.TXT\|\.G0[0-9]' || exit 1
    echo "DOS shared file hand-over integration passed"
fi
if [ -n "${SYSTEMSET:-}" ]; then
    mcopy -o -i "$P" ::/WINDOWS/SYSTEM.INI "$OUT/SYSTEM-AFTER.INI"
    if [ -n "${UNINST:-}" ]; then
        python3 -c 'import pathlib,sys; s=pathlib.Path(sys.argv[1]).read_text(errors="replace").lower(); assert "shell=progman.exe" in s; assert "gdi-test-owner=" not in s' "$OUT/SYSTEM-AFTER.INI" || exit 1
    else
        mcopy -o -i "$P" ::/TESTDRV/SYSTEM.SET "$OUT/SYSTEM.SET"
        python3 -c 'import pathlib,sys; s=pathlib.Path(sys.argv[1]).read_text(errors="replace").lower(); assert "shell=a:\\testdrv\\testvxd.386" in s; assert "gdi-test-owner=testdrv" in s' "$OUT/SYSTEM-AFTER.INI" || exit 1
    fi
    echo "DOS SYSTEM.INI Set integration passed"
fi
if [ -n "${HEXPATCH:-}" ]; then
    mcopy -o -i "$P" ::/WINDOWS/SYSTEM/WIN386.EXE "$OUT/PATCHED.EXE"
    mcopy -o -i "$P" ::/WINDOWS/SYSTEM/NDIS.386 "$OUT/NDIS-PATCHED.386"
    if [ -n "${UNINST:-}" ]; then
        # the original bytes are back, the backups of the patches are gone
        python3 -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]).read_bytes(); assert p.count(bytes.fromhex("66 C7 46 49 FF FF")) == 2 and b"\x6a\xff\x8f\x46\x49\x90" not in p' "$OUT/PATCHED.EXE" || exit 1
        python3 -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]).read_bytes(); assert p.count(bytes.fromhex("B9 00 00 10 00 90 E2 FE CD 20 3F 00 01 00 2B C2 B9 E8 03 00 00 F7 E1 A3")) == 1' "$OUT/NDIS-PATCHED.386" || exit 1
        ! mdir -b -i "$P" ::/WINDOWS/SYSTEM 2>/dev/null | grep -i '\.G0[0-9]$' || exit 1
    else
        mcopy -o -i "$P" ::/WINDOWS/SYSTEM/WIN386.G01 "$OUT/ORIGINAL.EXE"
        mcopy -o -i "$P" ::/WINDOWS/SYSTEM/NDIS.G01 "$OUT/NDIS-ORIGINAL.386"
        python3 -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]).read_bytes(); b=pathlib.Path(sys.argv[2]).read_bytes(); assert p.count(bytes.fromhex("6A FF 8F 46 49 90")) == 2; assert b.count(bytes.fromhex("66 C7 46 49 FF FF")) == 2' "$OUT/PATCHED.EXE" "$OUT/ORIGINAL.EXE" || exit 1
        python3 -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]).read_bytes(); b=pathlib.Path(sys.argv[2]).read_bytes(); assert p.count(bytes.fromhex("B9 00 00 40 06 E2 FE CD 20 3F 00 01 00 29 D0 75 01 40 6A 0A 59 F7 E1 A3")) == 1; assert b.count(bytes.fromhex("B9 00 00 10 00 90 E2 FE CD 20 3F 00 01 00 2B C2 B9 E8 03 00 00 F7 E1 A3")) == 1' "$OUT/NDIS-PATCHED.386" "$OUT/NDIS-ORIGINAL.386" || exit 1
    fi
    echo "DOS hexpatch integration passed"
fi
