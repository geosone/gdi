/* archive.c - GDA archives of the GeosOne DOS Installer: compressed files
 * on one or more disks (NAME.D01, NAME.D02, ...), made by mkgda.ps1.
 *
 * Disk file:   header, records, end mark
 *   header     "GDA1", set id (4), disk number (1), number of disks (1),
 *              reserved (2), total size of all files (4)
 *   record     'F', flags (1), method (1), name length (1), name,
 *              size (4), CRC-32 (4), DOS time (2), DOS date (2),
 *              size of the data part on this disk (4), data
 *              flags: 1 = continued from the previous disk,
 *                     2 = continued on the next disk
 *              method: 0 = stored, 8 = deflate (RFC 1951, raw)
 *   end mark   'E'
 * A file that does not fit on a disk continues in a record with flag 1 at
 * the start of the next disk; the data parts are simply concatenated.
 * Numbers are little endian.
 *
 * The inflate code follows the structure of puff.c by Mark Adler (zlib
 * contrib), written anew for a streaming 16-bit decoder.
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <fcntl.h>
#include <io.h>
#include <dos.h>
#include <malloc.h>
#include "gdi.h"

#define WSIZE 32768U

static struct {
	const char *base;          /* path without extension */
	unsigned long setid;
	int disk, ndisks;
	int fd;                    /* current disk file */
	unsigned char buf[2048];
	unsigned pos, len;
	unsigned long part;        /* data bytes left in the current record */
	int cont;                  /* current record continues on the next disk */
	char name[80];             /* name of the current record */
	unsigned long total, done;
	/* inflate */
	unsigned long bitbuf;
	int bitcnt;
	unsigned char __far *win;
	unsigned wpos;
	int out;                   /* output file handle, -1 = skip */
	unsigned long crc;
	unsigned long left;        /* bytes of the file still to produce */
	jmp_buf err;
	const char *errmsg;
} a;

static unsigned long crctab[256];

static void crc_init( void )
{
	unsigned long c;
	int n, k;
	for ( n = 0; n < 256; n++ ) {
		c = n;
		for ( k = 0; k < 8; k++ )
			c = c & 1 ? 0xEDB88320UL ^ ( c >> 1 ) : c >> 1;
		crctab[n] = c;
	}
}

static void error( const char *msg )
{
	a.errmsg = msg;
	longjmp( a.err, 1 );
}

static unsigned long get32( const unsigned char *p )
{
	return p[0] | (unsigned)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* --- raw input of the current disk --------------------------------- */

static int raw_byte( void )
{
	if ( a.pos == a.len ) {
		unsigned n;
		if ( _dos_read( a.fd, a.buf, sizeof(a.buf), &n ) || !n )
			error( "ReadError" );
		a.len = n;
		a.pos = 0;
	}
	return a.buf[a.pos++];
}

static void raw_read( unsigned char *p, unsigned n )
{
	while ( n-- )
		*p++ = raw_byte();
}

/* open disk n and check its header; 0 = ok */
static int open_disk( int n )
{
	char path[90];
	unsigned char h[16];
	unsigned got;
	sprintf( path, "%s.D%02d", a.base, n );
	if ( _dos_open( path, O_RDONLY, &a.fd ) )
		return -1;
	if ( _dos_read( a.fd, h, 16, &got ) || got != 16 || memcmp( h, "GDA1", 4 ) ||
		( n > 1 && get32( h + 4 ) != a.setid ) || h[8] != n ) {
		_dos_close( a.fd );
		return -1;
	}
	if ( n == 1 ) {
		a.setid = get32( h + 4 );
		a.ndisks = h[9];
		a.total = get32( h + 12 );
	}
	a.disk = n;
	a.pos = a.len = 0;
	return 0;
}

/* ask for disk n until it is there */
static void need_disk( int n )
{
	if ( a.fd >= 0 ) {
		_dos_close( a.fd );
		a.fd = -1;
	}
	while ( open_disk( n ) )
		if ( gdi_ask_disk( n ) )
			error( NULL );   /* cancelled */
}

/* record header; returns 0 at the end mark */
static int read_record( unsigned char *flags, unsigned char *method, unsigned long *size,
	unsigned long *crc, unsigned *time, unsigned *date )
{
	unsigned char h[4], d[16];
	int c = raw_byte();
	if ( c == 'E' )
		return 0;
	if ( c != 'F' )
		error( "BadArchive" );
	raw_read( h, 3 );
	*flags = h[0];
	*method = h[1];
	raw_read( (unsigned char *)a.name, h[2] );
	a.name[h[2]] = 0;
	raw_read( d, 16 );
	*size = get32( d );
	*crc = get32( d + 4 );
	*time = d[8] | d[9] << 8;
	*date = d[10] | d[11] << 8;
	a.part = get32( d + 12 );
	a.cont = ( *flags & 2 ) != 0;
	return 1;
}

/* next data byte of the current file, across disks */
static int data_byte( void )
{
	while ( !a.part ) {
		unsigned char fl, m;
		unsigned long s, c;
		unsigned t, d;
		char name[80];
		if ( !a.cont )
			error( "BadArchive" );
		strcpy( name, a.name );
		need_disk( a.disk + 1 );
		if ( !read_record( &fl, &m, &s, &c, &t, &d ) || !( fl & 1 ) || strcmp( name, a.name ) )
			error( "BadArchive" );
	}
	a.part--;
	return raw_byte();
}

/* --- output --------------------------------------------------------- */

static void flush( unsigned from, unsigned to )
{
	unsigned i, n;
	for ( i = from; i < to; i++ )
		a.crc = crctab[( a.crc ^ a.win[i] ) & 0xFF] ^ ( a.crc >> 8 );
	if ( a.out >= 0 && to > from )
		if ( _dos_write( a.out, a.win + from, to - from, &n ) || n != to - from )
			error( "WriteError" );
	a.done += to - from;
}

static void put( int c )
{
	if ( !a.left )
		error( "BadArchive" );
	a.left--;
	a.win[a.wpos++] = c;
	if ( a.wpos == WSIZE ) {
		flush( 0, WSIZE );
		a.wpos = 0;
		gdi_progress( a.done, a.total );
	}
}

/* --- inflate -------------------------------------------------------- */

static unsigned bits( int need )
{
	unsigned long v = a.bitbuf;
	while ( a.bitcnt < need ) {
		v |= (unsigned long)data_byte() << a.bitcnt;
		a.bitcnt += 8;
	}
	a.bitbuf = v >> need;
	a.bitcnt -= need;
	return (unsigned)( v & ( ( 1UL << need ) - 1 ) );
}

struct huff {
	short count[16];
	short *symbol;
};

static int decode( struct huff *h )
{
	int code = 0, first = 0, index = 0, len, count;
	for ( len = 1; len < 16; len++ ) {
		code |= bits( 1 );
		count = h->count[len];
		if ( code - count < first )
			return h->symbol[index + ( code - first )];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	error( "BadArchive" );
	return -1;
}

static void construct( struct huff *h, const short *length, int n )
{
	short offs[16];
	int s, len;
	for ( len = 0; len < 16; len++ )
		h->count[len] = 0;
	for ( s = 0; s < n; s++ )
		h->count[length[s]]++;
	offs[1] = 0;
	for ( len = 1; len < 15; len++ )
		offs[len + 1] = offs[len] + h->count[len];
	for ( s = 0; s < n; s++ )
		if ( length[s] )
			h->symbol[offs[length[s]]++] = s;
}

static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static short lsym[288], dsym[30];
static struct huff lcode = { { 0 }, lsym }, dcode = { { 0 }, dsym };
static short lengths[320];

static void codes( void )
{
	int sym;
	while ( ( sym = decode( &lcode ) ) != 256 ) {
		if ( sym < 256 )
			put( sym );
		else {
			unsigned len, dist, from;
			sym -= 257;
			if ( sym >= 29 )
				error( "BadArchive" );
			len = lbase[sym] + bits( lext[sym] );
			sym = decode( &dcode );
			if ( sym >= 30 )
				error( "BadArchive" );
			dist = dbase[sym] + bits( dext[sym] );
			from = ( a.wpos - dist ) & ( WSIZE - 1 );
			while ( len-- ) {
				put( a.win[from] );
				from = ( from + 1 ) & ( WSIZE - 1 );
			}
		}
	}
}

static void fixed( void )
{
	int s;
	for ( s = 0; s < 144; s++ ) lengths[s] = 8;
	for ( ; s < 256; s++ ) lengths[s] = 9;
	for ( ; s < 280; s++ ) lengths[s] = 7;
	for ( ; s < 288; s++ ) lengths[s] = 8;
	construct( &lcode, lengths, 288 );
	for ( s = 0; s < 30; s++ ) lengths[s] = 5;
	construct( &dcode, lengths, 30 );
	codes();
}

static void dynamic( void )
{
	static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	int nlen = bits( 5 ) + 257, ndist = bits( 5 ) + 1, ncode = bits( 4 ) + 4, i;
	if ( nlen > 286 || ndist > 30 )
		error( "BadArchive" );
	for ( i = 0; i < ncode; i++ )
		lengths[order[i]] = bits( 3 );
	for ( ; i < 19; i++ )
		lengths[order[i]] = 0;
	construct( &lcode, lengths, 19 );
	for ( i = 0; i < nlen + ndist; ) {
		int sym = decode( &lcode ), len = 0, rep;
		if ( sym < 16 ) {
			lengths[i++] = sym;
			continue;
		}
		if ( sym == 16 ) {
			if ( !i )
				error( "BadArchive" );
			len = lengths[i - 1];
			rep = 3 + bits( 2 );
		} else if ( sym == 17 )
			rep = 3 + bits( 3 );
		else
			rep = 11 + bits( 7 );
		if ( i + rep > nlen + ndist )
			error( "BadArchive" );
		while ( rep-- )
			lengths[i++] = len;
	}
	construct( &lcode, lengths, nlen );
	construct( &dcode, lengths + nlen, ndist );
	codes();
}

static void inflate( void )
{
	int last;
	a.bitbuf = 0;
	a.bitcnt = 0;
	do {
		int type;
		last = bits( 1 );
		type = bits( 2 );
		if ( type == 0 ) {
			unsigned len;
			a.bitbuf = 0;   /* to a byte boundary */
			a.bitcnt = 0;
			len = data_byte();
			len |= data_byte() << 8;
			data_byte();   /* one's complement of len */
			data_byte();
			while ( len-- )
				put( data_byte() );
		} else if ( type == 1 )
			fixed();
		else if ( type == 2 )
			dynamic();
		else
			error( "BadArchive" );
	} while ( !last );
}

/* --- public --------------------------------------------------------- */

/* install the files of the archive base.D01 ...: target() gives the path
 * for a name (0) or skips it (-1); returns NULL or an error text key */
const char *arc_install( const char *base, int (*target)( const char *name, char *path ) )
{
	unsigned char fl, method;
	unsigned long size, crc;
	unsigned time, date;
	char path[90];

	a.base = base;
	a.fd = -1;
	a.out = -1;
	a.done = 0;
	if ( !a.win && !( a.win = _fmalloc( WSIZE ) ) )
		return "NoMemory";
	crc_init();
	if ( setjmp( a.err ) ) {
		if ( a.out >= 0 )
			_dos_close( a.out );
		if ( a.fd >= 0 )
			_dos_close( a.fd );
		a.fd = -1;
		return a.errmsg ? a.errmsg : "Cancelled";
	}
	need_disk( 1 );
	for ( ;; ) {
		if ( !read_record( &fl, &method, &size, &crc, &time, &date ) ) {
			if ( a.disk < a.ndisks ) {   /* disks may end with a complete file */
				need_disk( a.disk + 1 );
				continue;
			}
			break;
		}
		if ( fl & 1 )
			error( "BadArchive" );
		a.out = -1;
		if ( !target( a.name, path ) ) {
			gdi_progress_name( a.name );
			if ( _dos_creat( path, _A_NORMAL, &a.out ) ) {
				a.out = -1;
				a.errmsg = path;
				error( "CreateError" );
			}
		}
		a.crc = 0xFFFFFFFFUL;
		a.left = size;
		a.wpos = 0;
		if ( method == 8 )
			inflate();
		else
			while ( a.left )
				put( data_byte() );
		/* the rest of the stored part and the remaining output */
		flush( 0, a.wpos );
		if ( a.left || ( a.crc ^ 0xFFFFFFFFUL ) != crc )
			error( "CrcError" );
		if ( a.part )   /* mkgda writes no data after the stream */
			error( "BadArchive" );
		if ( a.out >= 0 ) {
			_dos_setftime( a.out, date, time );
			_dos_close( a.out );
			a.out = -1;
		}
		gdi_progress( a.done, a.total );
	}
	_dos_close( a.fd );
	a.fd = -1;
	return NULL;
}

/* name of the file that failed (CreateError) */
const char *arc_errfile( void )
{
	return a.errmsg;
}
