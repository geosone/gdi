#!/bin/bash
# [Win.ini], [ProgMan], [Registry] and replaced files in Windows 3.x (plain
# DOS): installs test/TESTPM.INI from DOS, starts Windows (the runner GDIPxxxx.EXE
# applies the registry plan), removes it again with INSTALL /U from DOS and
# starts Windows again (the runner restores the registry).  Screens and the
# changed files go to build/win3x.
#   test/win3x.sh
# Windows is ended cleanly by test/exitwin/EXITWIN.EXE (WfW 3.11 writes its
# disk cache and WIN.INI only then); at the end the registry must contain the
# package entries ([Registry] Set= and Import=) and be as before after /U.
# FRONTEND=1 installs and removes from Windows (ENGINE=1: through the DOS
# engine; SILENT=1: INSTALL /S; MANAGE=1: removed by INSTALL /M), UPDATE=1
# updates.  SETUPDIR=1: [Setup] SetupDir=GDI, the installer and the
# journals in C:\PMTEST\GDI (with any of the others).
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
# COUNTRY=049: COUNTRY= in CONFIG.SYS (COUNTRYSYS: the COUNTRY.SYS of the DOS
# in the image) and the texts of the system's language, German: E=Entfernen
R=r
if [ -n "${COUNTRY:-}" ]; then
    COUNTRYSYS=${COUNTRYSYS:-$HOME/Iso/MSDOS-WfW/MS-Dos 7.1/Disk1/COUNTRY.SYS}
    sed -i '/^Texts=English/d' "$OUT/src/INSTALL.INI"
    R=e
fi
# SETUPDIR=1: the installer and its journals in C:\PMTEST\GDI
SD=PMTEST
if [ -n "${SETUPDIR:-}" ]; then
    sed -i 's/^DefaultDir=%BOOT%\\PMTEST\r$/&\nSetupDir=GDI\r/' "$OUT/src/INSTALL.INI"
    grep -q '^SetupDir=GDI' "$OUT/src/INSTALL.INI" || { echo "SetupDir= not set"; exit 1; }
    SD=PMTEST/GDI
fi
U="C:\\${SD//\//\\}\\INSTALL.EXE /U"
# ENGINE=1: [Detect] needs the DOS engine, the frontend starts it
[ -n "${ENGINE:-}" ] && printf '\r\n[Detect]\r\nGDITRUE=TESTTOOL.COM\r\n' >> "$OUT/src/INSTALL.INI"
printf '\xcd\x20' > "$OUT/src/TESTTOOL.COM"
echo 'GDI test readme' | crlf > "$OUT/src/README.TXT"
echo 'setting=default' | crlf > "$OUT/src/PMTEST.CFG"
echo 'replaced by GDI' | crlf > "$OUT/src/REPL.TXT"
echo 'original file' | crlf > "$OUT/ORIG.TXT"
printf 'REGEDIT\n; [Registry] Import=\nHKEY_CLASSES_ROOT\\GdiTestFile = GDI Testdatei\nHKEY_CLASSES_ROOT\\GdiTestFile\\shell\\open\\command = notepad.exe %%1\nHKEY_CLASSES_ROOT\\GdiTestFile\\NoValue\n' | crlf > "$OUT/src/PMTEST.REG"
(
    export WATCOM=${WATCOM:-/opt/watcom}; export INCLUDE="$WATCOM/h:$WATCOM/h/win" PATH="$WATCOM/binl64:$PATH"
    cd "$OUT" && wcc -q -bt=windows -ms -0 -os -zq -fo=EXITWIN.OBJ "$ROOT/test/exitwin/EXITWIN.C" &&
        wlink option quiet system windows option stack=4k,heapsize=1k name EXITWIN.EXE file EXITWIN.OBJ
) || exit 1
mmd -i "$P" ::/SRC 2>/dev/null
mcopy -o -i "$P" "$OUT"/src/* ::/SRC/ || exit 1
mcopy -o -i "$P" "$OUT/ORIG.TXT" ::/WINDOWS/REPL.TXT
mcopy -o -i "$P" "$OUT/EXITWIN.EXE" ::/
if [ -n "${COUNTRY:-}" ]; then
    mcopy -o -i "$P" "$COUNTRYSYS" ::/COUNTRY.SYS || exit 1
    { mtype -i "$P" ::/CONFIG.SYS | tr -d '\r'; echo "COUNTRY=$COUNTRY,850,C:\COUNTRY.SYS"; } | crlf > "$OUT/CONFIG.SYS"
    mcopy -o -i "$P" "$OUT/CONFIG.SYS" ::/
fi

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
    for f in WINDOWS/WIN.INI WINDOWS/SYSTEM.INI WINDOWS/CONTROL.INI WINDOWS/PROGMAN.INI WINDOWS/REPL.TXT WINDOWS/REG.DAT; do mcopy -o -i "$P" "::/$f" "$d/" 2>/dev/null; done
    mcopy -o -i "$P" "::/WINDOWS/*.GRP" "$d/" 2>/dev/null
    mcopy -o -s -i "$P" "::/PMTEST" "$d/" 2>/dev/null
}
fetch 0-before

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
    run "$U" uninst "$R ret ret ret"
    fetch 3-removed
    mcopy -o -i "$P" ::/GDI.INI "$OUT/files/3-removed/" 2>/dev/null
elif [ -n "${FRONTEND:-}" ]; then
    # the same from the Win16 frontend: WIN.INI and the groups through
    # Windows (profile functions, DDE), the registry right away
    # the same from Windows: the installer core runs in the frontend with
    # windows of its own (ENGINE=1: the frontend's window, then the DOS
    # engine); WIN.INI and the groups through Windows (profile functions,
    # DDE), the registry right away
    W='@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN C:\EXITWIN.EXE 100'
    if [ -n "${ENGINE:-}" ]; then
        KEYS_IN="ret ret ret ret ret ret ret ret" KEYS_UN="ret $R ret ret"
    else
        KEYS_IN="ret ret ret ret ret ret" KEYS_UN="$R ret"
    fi
    A_IN='' A_UN=$U
    [ -n "${SILENT:-}" ] && A_IN=' /S' KEYS_IN=''
    [ -n "${MANAGE:-}" ] && A_UN='C:\SRC\INSTALL.EXE /M' KEYS_UN="spc ret $R ret"
    TIMEOUT=${TIMEOUT:-80} run "$W C:\SRC\INSTALL.EXE$A_IN" install "$KEYS_IN"
    fetch 1-installed
    TIMEOUT=${TIMEOUT:-80} run "$W $A_UN" uninst "$KEYS_UN"
    fetch 2-removed
    INSTALLED=1-installed REMOVED=2-removed
else
    run 'C:\SRC\INSTALL.EXE' install "ret ret ret ret ret ret ret"
    fetch 1-installed
    run '@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN C:\EXITWIN.EXE 30' windows ""
    fetch 2-windows
    run "$U" uninst "$R ret ret ret"
    fetch 3-removed
    run '@ECHO OFF|PATH C:\;C:\WINDOWS|C:\WINDOWS\WIN C:\EXITWIN.EXE 30' windows2 ""
    fetch 4-windows
    INSTALLED=2-windows REMOVED=4-windows
fi
ls -R "$OUT/files"
if [ -z "${UPDATE:-}" ]; then
    # the registry: the package entries while installed, as before after /U
    cd "$OUT/files"
    # [Run] Windows=: from DOS RUNONCE.PLN, started by the runner
    if [ "$INSTALLED" = 2-windows ]; then
        [ -f 1-installed/$SD/RUNONCE.PLN ] || { echo "RUNONCE.PLN missing"; exit 1; }
        [ ! -f 2-windows/$SD/RUNONCE.PLN ] || { echo "RUNONCE.PLN not started"; exit 1; }
    fi
    "$ROOT/test/regdat.py" "$INSTALLED/REG.DAT" > installed.reg
    for k in '.gdt = GdiTestFile' 'GdiTestFile = GDI Testdatei' 'GdiTestFile\shell\open\command = notepad.exe %1'; do
        grep -qxF ".classes\\$k" installed.reg || { echo "registry entry missing: $k"; exit 1; }
    done
    diff <("$ROOT/test/regdat.py" 0-before/REG.DAT) <("$ROOT/test/regdat.py" "$REMOVED/REG.DAT") ||
        { echo "registry not restored"; exit 1; }
    # -B: Windows writes a blank line before a new section and keeps it
    # when the profile functions remove the section again
    diff -B <(tr -d '\r' < 0-before/WIN.INI) <(tr -d '\r' < "$REMOVED/WIN.INI") || { echo "WIN.INI not restored"; exit 1; }
    # %ACCESSORIES%: the item is in the accessories group of the German Windows
    grep -aq 'GDI Test' "$INSTALLED"/ZUBEH*.GRP || { echo "%ACCESSORIES% item missing"; exit 1; }
    # [Ini] Set=: CONTROL.INI
    grep -qx 'Value=installed' <(tr -d '\r' < "$INSTALLED/CONTROL.INI") || { echo "CONTROL.INI value missing"; exit 1; }
    diff -B <(tr -d '\r' < 0-before/CONTROL.INI) <(tr -d '\r' < "$REMOVED/CONTROL.INI") || { echo "CONTROL.INI not restored"; exit 1; }
    # the setup directory: INSTALL.INI there, the files in C:\PMTEST; all
    # gone after the removal
    [ -f "1-installed/$SD/INSTALL.INI" ] && [ -f 1-installed/PMTEST/TESTTOOL.COM ] || { echo "setup directory wrong"; exit 1; }
    [ ! -e "$REMOVED/PMTEST" ] || { echo "C:\PMTEST left"; ls -R "$REMOVED/PMTEST"; exit 1; }
    echo "Windows registry, WIN.INI and CONTROL.INI integration passed"
fi
