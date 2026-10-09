# Boot sector for MS-DOS 7.x (Win9x IO.SYS) floppies

`bs9x.asm` is our own FAT12/FAT16 boot sector for the `IO.SYS` of
MS-DOS 7.x.  It sets up the state IO.SYS expects from the Win9x boot
sector (diskette parameter table at 0:0522h, original INT 1Eh vector on
the stack, BP = 7C00h, [BP-4] = first data sector, DI = first cluster),
loads the first 4 sectors of IO.SYS to 0070:0000 and jumps to 0070:0200.

    nasm -f bin -o bs9x.bin bs9x.asm

The 512 bytes are embedded in `../GdiDisk.cs` (`BootSectors.Win9xFat12`);
`Fat12Image` fills in the BPB (bytes 11..61) when it writes an image.
`make bootsector` in the repository root rebuilds and re-embeds it.

GNU General Public License version 3.
