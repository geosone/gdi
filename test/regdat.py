#!/usr/bin/env python3
# Dumps a Windows 3.1 REG.DAT (SHCC3.10) as sorted "key = value" lines, so
# that the Windows tests can compare the registry before and after (Windows
# keeps the free space in the file, the bytes differ).
#   test/regdat.py REG.DAT
import struct, sys
def dump(fn):
    d = open(fn, 'rb').read()
    assert d[:8] == b'SHCC3.10'
    tab1, tab2, cnt, textoff, textsize = struct.unpack_from('<5I', d, 8)
    def ent(i): return struct.unpack_from('<4H', d, tab1 + 8 * i)
    def text(i):
        if not i: return None
        h, ref, ln, off = ent(i)
        return d[textoff + off:textoff + off + ln].decode('cp1252')
    out = []
    def walk(i, path, depth=0):
        while i and depth < 64:
            sib, child, key, val = ent(i)
            k = text(key) or ''
            p = path + '\\' + k if path else k
            v = text(val)
            out.append('%s = %s' % (p, v if v is not None else '<none>'))
            walk(child, p, depth + 1)
            i = sib
    walk(ent(0)[1], '')
    return out
for l in sorted(dump(sys.argv[1])): print(l)
