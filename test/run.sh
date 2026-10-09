#!/bin/bash
# Test INSTALL.EXE in QEMU: boots DOS (floppy image), runs INSTALL with
# test/TEST.INI (or INI=...), keys via the QEMU monitor (KEYS), shows the
# changed CONFIG.SYS/AUTOEXEC.BAT and the target directory on COM1.
#   test/run.sh            install
#   UNINST=1 test/run.sh   install, then INSTALL /U from the target directory
#   PKG=dir TARGET=NAME test/run.sh   install a distribution directory
#                       (all files of dir, its INSTALL.INI) into A:\NAME
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
if [ -n "${PKG:-}" ]; then
    cp "$PKG"/* "$OUT/src/"
else
    cp "$ROOT/build/INSTALL.EXE" "$OUT/src/"
    cp "$INI" "$OUT/src/INSTALL.INI"
    cp "$ROOT/LICENSE" "$OUT/src/LICENSE.TXT"
    printf 'TESTDRV readme\nline 2\n' | crlf > "$OUT/src/README.TXT"
    cp "$ROOT/build/INSTALL.EXE" "$OUT/src/TESTDRV.SYS"
    printf '\xcd\x20' > "$OUT/src/TESTTOOL.COM"      # INT 20h
    cp "$ROOT/build/INSTALL.EXE" "$OUT/src/TESTVXD.386"
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
    echo 'A:\SRC\INSTALL.EXE'
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
mcopy -o -i "$P" "$OUT"/src/* ::/SRC/ && mcopy -o -i "$P" "$OUT/CONFIG.SYS" "$OUT/AUTOEXEC.BAT" ::/ || exit 1
if [ -n "${WINDOWS:-}" ]; then   # a fake Windows directory with SYSTEM.INI
    mmd -i "$P" ::/WINDOWS
    printf '\xcd\x20' > "$OUT/WIN.COM"
    printf '[boot]\r\nshell=progman.exe\r\n\r\n[386Enh]\r\ndevice=*vpicd\r\n\r\n[NonWindowsApp]\r\n' > "$OUT/SYSTEM.INI"
    mcopy -o -i "$P" "$OUT/WIN.COM" "$OUT/SYSTEM.INI" ::/WINDOWS/
    sed -i 's/^A:\\SRC\\QEXIT/ECHO === SYSTEM.INI > AUX\r\nTYPE A:\\WINDOWS\\SYSTEM.INI > AUX\r\nA:\\SRC\\QEXIT/' "$OUT/src/RUN.BAT"
    mcopy -o -i "$P" "$OUT/src/RUN.BAT" ::/SRC/
fi
KEYS=${KEYS:-"ret ret ret ret ret ret ret"}
[ -n "${UNINST:-}" ] && KEYS="$KEYS r ret"
{
    sleep ${START:-8}; n=0
    for k in $KEYS; do n=$((n+1)); echo "screendump $OUT/s$n.ppm"; sleep 1; echo "sendkey $k"; sleep 2; done
    sleep ${TIMEOUT:-10}; echo "screendump $OUT/end.ppm"; sleep 1; echo quit
} | qemu-system-i386 -machine pc -accel kvm -m 16 -display none -vga std -monitor stdio \
    -drive if=floppy,format=raw,file="$P" -boot a -nic none -serial file:"$OUT/serial.log" \
    -device isa-debug-exit,iobase=0x501,iosize=0x02 > "$OUT/monitor.log" 2>&1
for f in "$OUT"/*.ppm; do [ -f "$f" ] && magick "$f" "${f%.ppm}.png" && rm -f "$f"; done
tr -d '\r' < "$OUT/serial.log"
