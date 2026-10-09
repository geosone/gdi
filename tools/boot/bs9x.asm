; bs9x.asm - FAT12/FAT16 boot sector for the IO.SYS of MS-DOS 7.x (Win9x)
;
; Loads the first 4 sectors of IO.SYS (found in the first root directory
; sector) to 0070:0000 and jumps to 0070:0200 in the state IO.SYS expects
; from the Win9x boot sector:
;   BP = 7C00h (BPB), [BP-4] = first data sector (dword), DI = first
;   cluster of IO.SYS, DL = boot drive (also in [BP+24h]),
;   the diskette parameter table copied to 0:0522h (INT 1Eh points to it,
;   EOT = sectors per track, head settle 15 ms), the original INT 1Eh
;   vector and its address 0:0078h on the stack (SP = 7BF4h).
; IO.SYS must start in contiguous sectors (true for the first 4 sectors
; of a file the image writer places contiguously).
;
;   nasm -f bin -o bs9x.bin bs9x.asm
; The BPB (bytes 11..61) is filled in by Fat12Image (GdiDisk.cs).
;
; Copyright (C) 2026 GeosOne.  GNU General Public License version 3.

        cpu     8086
        org     7C00h

        jmp     short start
        nop
        db      "MSWIN4.1"              ; OEM name
        times   3Eh - ($ - $$) db 0     ; BPB + extended BPB (by the writer)

BPS     equ     0Bh                     ; offsets from BP = 7C00h
SPC     equ     0Dh
RSV     equ     0Eh
NFATS   equ     10h
ROOTENT equ     11h
FATSZ   equ     16h
SPT     equ     18h
HEADS   equ     1Ah
HIDDEN  equ     1Ch
DRIVE   equ     24h

start:  xor     cx, cx
        mov     ss, cx
        mov     sp, 7BFCh
        push    ss
        pop     es
        mov     bp, 78h                 ; INT 1Eh vector
        lds     si, [bp]
        push    ds                      ; original vector on the stack
        push    si
        push    ss
        push    bp
        mov     di, 522h                ; copy the table to 0:0522h
        mov     [bp], di
        mov     [bp+2], cx
        mov     cl, 11
        cld
        rep     movsb
        push    es
        pop     ds                      ; DS = ES = SS = 0
        mov     bp, 7C00h
        mov     byte [di-2], 0Fh        ; head settle time
        mov     ax, [bp+SPT]
        mov     [526h], al              ; EOT = sectors per track
        mov     [bp+DRIVE], dl

        ; root directory: hidden + reserved + FATs * FAT size
        mov     al, [bp+NFATS]
        cbw
        mul     word [bp+FATSZ]
        add     ax, [bp+HIDDEN]
        adc     dx, [bp+HIDDEN+2]
        add     ax, [bp+RSV]
        adc     dx, cx                  ; CX = 0
        push    dx
        push    ax
        mov     di, 700h                ; first root directory sector
        mov     cx, 1                   ;   to 0:0700h
        call    read
        jc      fail
        ; data area: root + ceil(root entries * 32 / bytes per sector)
        mov     ax, 32
        mul     word [bp+ROOTENT]
        mov     bx, [bp+BPS]
        add     ax, bx
        dec     ax
        xor     dx, dx
        div     bx
        pop     bx
        pop     dx                      ; DX:BX = root directory
        add     ax, bx
        adc     dx, 0
        mov     [bp-4], ax              ; first data sector
        mov     [bp-2], dx

        mov     di, 700h
        mov     cx, 16                  ; entries in one 512-byte sector
search: push    cx
        push    di
        mov     si, ioname
        mov     cx, 11
        repe    cmpsb
        pop     di
        pop     cx
        je      found
        add     di, 32
        loop    search
        jmp     short fail

found:  mov     di, [di+1Ah]            ; first cluster
        lea     ax, [di-2]
        mov     cl, [bp+SPC]
        xor     ch, ch
        mul     cx
        add     ax, [bp-4]
        adc     dx, [bp-2]              ; DX:AX = first sector of IO.SYS
        push    di
        mov     di, 700h                ; to 0070:0000
        mov     cx, 4
        call    read
        pop     di
        jc      fail
        mov     dl, [bp+DRIVE]
        jmp     70h:200h

fail:   mov     sp, 7BF4h
        mov     si, msg
.next:  lodsb
        or      al, al
        jz      .key
        mov     ah, 0Eh
        mov     bx, 7
        int     10h
        jmp     short .next
.key:   xor     ax, ax
        int     16h
        pop     si                      ; restore INT 1Eh
        pop     ds
        pop     word [si]
        pop     word [si+2]
        int     19h

; read CX sectors from DX:AX (LBA) to 0:DI; CF on error
read:   push    ax
        push    dx
        push    cx
        mov     bx, di
        div     word [bp+SPT]           ; AX = track, DX = sector - 1
        inc     dl
        mov     cl, dl
        xor     dx, dx
        div     word [bp+HEADS]         ; AX = cylinder, DX = head
        mov     dh, dl
        mov     ch, al
        ror     ah, 1
        ror     ah, 1
        or      cl, ah
        mov     dl, [bp+DRIVE]
        mov     si, 3                   ; tries
.try:   mov     ax, 201h
        int     13h
        jnc     .ok
        xor     ax, ax
        int     13h
        dec     si
        jnz     .try
        pop     cx
        pop     dx
        pop     ax
        stc
        ret
.ok:    pop     cx
        pop     dx
        pop     ax
        add     di, [bp+BPS]
        add     ax, 1
        adc     dx, 0
        loop    read
        clc
        ret

ioname  db      "IO      SYS"
msg     db      13, 10, "Kein System / No system: IO.SYS", 13, 10, 0

        times   510 - ($ - $$) db 0
        dw      0AA55h
