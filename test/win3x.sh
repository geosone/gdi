#!/bin/bash
# [Win.ini], [ProgMan], [Registry] and replaced files in Windows 3.x (plain
# DOS): installs test/TESTPM.INI from DOS, starts Windows (GDIPOST.EXE
# applies the registry plan), removes it again with INSTALL /U from DOS and
# starts Windows again (GDIPOST.EXE restores the registry).  Screens and the
# changed files go to build/win3x.
#   test/win3x.sh
# Environment: WIN311IMG (a hard disk image with Windows 3.1x in C:\WINDOWS,
# FAT partition at sector 63, AUTOEXEC.BAT is replaced), TIMEOUT
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/win3x
IMG=${WIN311IMG:-$HOME/mars/ahciaspi/build/win311/win311.img}
[ -f "$IMG" ] || { echo "WIN311IMG not found: $IMG"; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT/src" "$OUT/files"
cp "$IMG" "$OUT/hd.img"; P="$OUT/hd.img@@32256"
crlf() { sed 's/$/\r/'; }
cp "$ROOT/build/INSTALL.EXE" "$OUT/src/"
cp "$ROOT/test/TESTPM.INI" "$OUT/src/INSTALL.INI"
printf '\xcd\x20' > "$OUT/src/TESTTOOL.COM"
echo 'GDI test readme' | crlf > "$OUT/src/README.TXT"
echo 'setting=default' | crlf > "$OUT/src/PMTEST.CFG"
echo 'replaced by GDI' | crlf > "$OUT/src/REPL.TXT"
echo 'original file' | crlf > "$OUT/ORIG.TXT"
mmd -i "$P" ::/SRC 2>/dev/null
mcopy -o -i "$P" "$OUT"/src/* ::/SRC/ || exit 1
mcopy -o -i "$P" "$OUT/ORIG.TXT" ::/WINDOWS/REPL.TXT

# one run: AUTOEXEC.BAT = $1 (| separated), keys, screens $2-N.png
run() {
    echo "$1" | tr '|' '\n' | crlf > "$OUT/AUTOEXEC.BAT"
    mcopy -o -i "$P" "$OUT/AUTOEXEC.BAT" ::/
    {
        sleep ${START:-6}; n=0
        for k in $3; do n=$((n+1)); echo "screendump $OUT/$2-$n.ppm"; sleep 1; echo "sendkey $k"; sleep ${STEP:-2}; done
        sleep ${TIMEOUT:-30}; echo "screendump $OUT/$2-end.ppm"; sleep 1; echo quit
    } | qemu-system-i386 -machine pc -accel kvm -m 32 -display none -vga std -monitor stdio \
        -drive if=ide,index=0,format=raw,file="$OUT/hd.img" -boot c -nic none > "$OUT/monitor.log" 2>&1
    for f in "$OUT"/$2-*.ppm; do [ -f "$f" ] && magick "$f" "${f%.ppm}.png" && rm -f "$f"; done
}
fetch() {
    d="$OUT/files/$1"; mkdir -p "$d"
    for f in WINDOWS/WIN.INI WINDOWS/SYSTEM.INI WINDOWS/PROGMAN.INI WINDOWS/REPL.TXT WINDOWS/REG.DAT; do mcopy -o -i "$P" "::/$f" "$d/" 2>/dev/null; done
    mcopy -o -i "$P" "::/WINDOWS/*.GRP" "$d/" 2>/dev/null
    mcopy -o -s -i "$P" "::/PMTEST" "$d/" 2>/dev/null
}

if [ -n "${UPDATE:-}" ]; then
    # version 2.0 over 1.0: the files are replaced, a changed settings file
    # and the system settings stay
    run 'C:\SRC\INSTALL.EXE' install "ret ret ret ret ret ret ret"
    echo 'setting=changed by the user' | crlf > "$OUT/PMTEST.CFG"
    mcopy -o -i "$P" "$OUT/PMTEST.CFG" ::/PMTEST/
    mcopy -o -i "$P" ::/WINDOWS/WIN.INI "$OUT/WIN.INI"
    sed -i 's/^Value=installed/Value=changed by the user/' "$OUT/WIN.INI"
    mcopy -o -i "$P" "$OUT/WIN.INI" ::/WINDOWS/
    fetch 1-installed
    sed -i 's/^Version=1.0/Version=2.0/; s/^\(Item=1|GDI Testgruppe|Editor Test.*\)$/\1\nItem=1|GDI Testgruppe|Neues Tool|%DIR%\\TESTTOOL.COM\r/; s/^\(Set=1|windows|Beep=no.*\)$/\1\nSet=1|GdiTest|Neu=2.0\r\n\r\n[System.ini]\r\nRemove=gditest=\r\nAdd=1|GdiTest|gditest=2.0\r/' "$OUT/src/INSTALL.INI"
    echo 'GDI test readme 2.0' | crlf > "$OUT/src/README.TXT"
    echo 'setting=default 2.0' | crlf > "$OUT/src/PMTEST.CFG"
    mcopy -o -i "$P" "$OUT"/src/* ::/SRC/
    if [ -n "${FRONTEND:-}" ]; then
        run '@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN C:\SRC\INSTALL.EXE' update "ret ret ret ret ret ret"
    else
        run 'C:\SRC\INSTALL.EXE' update "ret ret ret ret ret"
    fi
    fetch 2-updated
    mcopy -o -i "$P" ::/GDI.INI "$OUT/files/2-updated/" 2>/dev/null
    run 'C:\PMTEST\INSTALL.EXE /U' uninst "r ret ret ret"
    fetch 3-removed
    mcopy -o -i "$P" ::/GDI.INI "$OUT/files/3-removed/" 2>/dev/null
elif [ -n "${FRONTEND:-}" ]; then
    # the same from the Win16 frontend: WIN.INI and the groups through
    # Windows (profile functions, DDE), the registry right away
    W='@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN'
    run "$W C:\SRC\INSTALL.EXE" install "ret ret ret ret ret ret ret ret"
    fetch 1-installed
    run "$W C:\PMTEST\INSTALL.EXE /U" uninst "ret r ret ret"
    fetch 2-removed
else
    run 'C:\SRC\INSTALL.EXE' install "ret ret ret ret ret ret ret"
    fetch 1-installed
    run '@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN' windows ""
    fetch 2-windows
    run 'C:\PMTEST\INSTALL.EXE /U' uninst "r ret ret ret"
    fetch 3-removed
    run '@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN' windows2 ""
    fetch 4-windows
fi
ls -R "$OUT/files"
