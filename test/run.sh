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
printf 'org 100h\nmov dx,501h\nout dx,al\nint 20h\n' > "$OUT/qexit.asm" && nasm -f bin -o "$OUT/src/QEXIT.COM" "$OUT/qexit.asm"
if [ -n "${MENU:-}" ]; then   # multi-config CONFIG.SYS
    { echo '[menu]'; echo 'menuitem=STD,Standard'; echo 'menudefault=STD,0'; echo '[STD]'; echo 'DEVICE=A:\HIMEM.SYS'; echo '[common]'; echo 'FILES=30'; } | crlf > "$OUT/CONFIG.SYS"
else
    { echo 'DEVICE=A:\HIMEM.SYS'; echo 'DOS=HIGH'; echo 'FILES=30'; } | crlf > "$OUT/CONFIG.SYS"
fi
# the steps run in RUN.BAT: INSTALL changes AUTOEXEC.BAT while it runs
{
    echo '@ECHO OFF'
    echo "A:\\SRC\\INSTALL.EXE $ARGS"
    [ -n "${UNINST:-}" ] && echo "A:\\$TARGET\\INSTALL.EXE /U"
    echo 'ECHO === CONFIG.SYS > AUX'; echo 'TYPE A:\CONFIG.SYS > AUX'
    echo 'ECHO === AUTOEXEC.BAT > AUX'; echo 'TYPE A:\AUTOEXEC.BAT > AUX'
    echo 'ECHO === TARGET > AUX'; echo "DIR A:\\$TARGET > AUX"
    echo 'ECHO === BACKUPS > AUX'; echo 'DIR A:\*.G0? > AUX'
    echo 'A:\SRC\QEXIT'
} | crlf > "$OUT/src/RUN.BAT"
{ echo '@ECHO OFF'; echo 'A:\SRC\RUN.BAT'; } | crlf > "$OUT/AUTOEXEC.BAT"
P="$OUT/boot.img"
mmd -i "$P" ::/SRC 2>/dev/null
mcopy -s -o -i "$P" "$OUT"/src/* ::/SRC/ && mcopy -o -i "$P" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/ || exit 1
if [ -n "${WINDOWS:-}" ]; then   # a fake Windows directory with SYSTEM.INI
    mmd -i "$P" ::/WINDOWS
    printf '\xcd\x20' > "$OUT/WIN.COM"
    printf '[boot]\r\nshell=progman.exe\r\n\r\n[386Enh]\r\ndevice=*vpicd\r\n\r\n[NonWindowsApp]\r\n' > "$OUT/SYSTEM.INI"
    mcopy -o -i "$P" "$OUT/WIN.COM" "$OUT/SYSTEM.INI" ::/WINDOWS/
    sed -i 's/^A:\\SRC\\QEXIT/ECHO === SYSTEM.INI > AUX\r\nTYPE A:\\WINDOWS\\SYSTEM.INI > AUX\r\nA:\\SRC\\QEXIT/' "$OUT/src/RUN.BAT"
    mcopy -o -i "$P" "$OUT/src/RUN.BAT" ::/SRC/
fi
if [ -n "${HEXPATCH:-}" ]; then
    mmd -i "$P" ::/WINDOWS/SYSTEM
    printf '\x10\x66\xC7\x46\x49\xFF\xFF\x20\x66\xC7\x46\x49\xFF\xFF\x30' > "$OUT/WIN386.EXE"
    printf '\x10\xB9\x00\x00\x10\x00\x90\xE2\xFE\xCD\x20\x3F\x00\x01\x00\x2B\xC2\xB9\xE8\x03\x00\x00\xF7\xE1\xA3\x20' > "$OUT/NDIS.386"
    mcopy -o -i "$P" "$OUT/WIN386.EXE" "$OUT/NDIS.386" ::/WINDOWS/SYSTEM/
fi
DEFAULT_KEYS="ret ret ret ret ret ret ret ret ret"
[ -n "${UNINST:-}" ] && DEFAULT_KEYS="ret ret ret ret ret ret"
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
