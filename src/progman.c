/* progman.c - Windows 3.x Program Manager groups (.GRP files) for GDI
 *
 * The DOS engine changes the groups directly in their files while Windows is
 * not running; the Win16 frontend does the same through DDE.  Both record
 * the changes in PROGMAN.LOG of the installation, see README.md.
 *
 * Group file (Windows 3.1 SDK, "PMCC"): header of 34 bytes, a table of item
 * offsets (0 = free slot), the group name, the items and their strings and
 * icons, then optional tag data from offset cbGroup.  The checksum makes the
 * sum of all words of the file zero.  An item added here has no icon data
 * and the header's icon format is set to 0: Program Manager then extracts
 * all icons of the group again from their files when it loads the group, as
 * it does after a display change.
 *
 * Portable C89: the files are processed in small pieces (DOS small model).
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "progman.h"

#define HDR      34          /* size of the group header */
#define ITEM     24          /* size of an item record */
#define MAXITEMS 200         /* item slots of a group */
#define MAXINI   120         /* lines of PROGMAN.INI */
#define MAXGROUP 40          /* groups of Program Manager */
#define COLUMNS  5           /* icons per row of a new item's position */

#ifdef __WATCOMC__
#define SEP "\\"
#else
#define SEP "/"                /* host tests */
#endif

static unsigned get16( const unsigned char *p )
{
	return p[0] | ( (unsigned)p[1] << 8 );
}

static void put16( unsigned char *p, unsigned v )
{
	p[0] = (unsigned char)( v & 0xFF );
	p[1] = (unsigned char)( v >> 8 );
}

static int same( const char *a, const char *b )
{
	while ( *a && *b ) {
		if ( toupper( (unsigned char)*a ) != toupper( (unsigned char)*b ) )
			return 0;
		a++;
		b++;
	}
	return *a == *b;
}

static void path_cat( char *out, const char *dir, const char *name )
{
	size_t n;
	sprintf( out, "%.100s", dir );
	n = strlen( out );
	if ( n && out[n - 1] != '\\' && out[n - 1] != '/' )
		strcat( out, SEP );
	n = strlen( out );
	sprintf( out + n, "%.*s", (int)( GDI_PM_PATH - n - 1 ), name );
}

/* code page 850 -> 1252 for 80h..FFh (0 = no equivalent: '_') */
static const unsigned char cp850[128] = {
	0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
	0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xF8, 0xA3, 0xD8, 0xD7, 0x83,
	0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0xAE, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
	0, 0, 0, 0, 0, 0xC1, 0xC2, 0xC0, 0xA9, 0, 0, 0, 0, 0xA2, 0xA5, 0,
	0, 0, 0, 0, 0, 0, 0xE3, 0xC3, 0, 0, 0, 0, 0, 0, 0, 0xA4,
	0xF0, 0xD0, 0xCA, 0xCB, 0xC8, 0, 0xCD, 0xCE, 0xCF, 0, 0, 0, 0, 0xA6, 0xCC, 0,
	0xD3, 0xDF, 0xD4, 0xD2, 0xF5, 0xD5, 0xB5, 0xFE, 0xDE, 0xDA, 0xDB, 0xD9, 0xFD, 0xDD, 0xAF, 0xB4,
	0xAD, 0xB1, 0, 0xBE, 0xB6, 0xA7, 0xF7, 0xB8, 0xB0, 0xA8, 0xB7, 0xB9, 0xB3, 0xB2, 0, 0xA0
};

void gdi_pm_oem_to_ansi( char *text )
{
	unsigned char *p = (unsigned char *)text;
	for ( ; *p; p++ )
		if ( *p >= 0x80 )
			*p = cp850[*p - 0x80] ? cp850[*p - 0x80] : '_';
}

/* back to code page 850 (the text mode), '_' for a missing character */
void gdi_pm_ansi_to_oem( char *text )
{
	unsigned char *p = (unsigned char *)text;
	int i;
	for ( ; *p; p++ )
		if ( *p >= 0x80 ) {
			for ( i = 0; i < 128 && cp850[i] != *p; i++ )
				;
			*p = i < 128 ? (unsigned char)( 0x80 + i ) : '_';
		}
}

/* --- PROGMAN.INI ---------------------------------------------------------- */

static char *ini[MAXINI];
static int nini;

static void ini_clear( void )
{
	while ( nini )
		free( ini[--nini] );
}

static int ini_insert( int at, const char *text )
{
	int i;
	char *s;
	if ( nini == MAXINI || !( s = (char *)malloc( strlen( text ) + 1 ) ) )
		return -1;
	strcpy( s, text );
	for ( i = nini; i > at; i-- )
		ini[i] = ini[i - 1];
	ini[at] = s;
	nini++;
	return 0;
}

static void ini_delete( int at )
{
	free( ini[at] );
	for ( ; at < nini - 1; at++ )
		ini[at] = ini[at + 1];
	nini--;
}

static int ini_read( const char *windir )
{
	char path[GDI_PM_PATH], buf[200];
	FILE *f;
	size_t n;
	ini_clear();
	path_cat( path, windir, "PROGMAN.INI" );
	if ( !( f = fopen( path, "r" ) ) )
		return 0;   /* none yet: a new one is written */
	while ( fgets( buf, sizeof(buf), f ) ) {
		n = strlen( buf );
		while ( n && ( buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == 0x1A ) )
			buf[--n] = 0;
		if ( ini_insert( nini, buf ) ) {
			fclose( f );
			ini_clear();
			return -1;
		}
	}
	fclose( f );
	return 0;
}

static int ini_write( const char *windir )
{
	char path[GDI_PM_PATH];
	FILE *f;
	int i, error = 0;
	path_cat( path, windir, "PROGMAN.INI" );
	if ( !( f = fopen( path, "w" ) ) )
		return -1;
	for ( i = 0; i < nini; i++ )
		if ( fprintf( f, "%s\n", ini[i] ) < 0 )
			error = 1;
	return fclose( f ) || error ? -1 : 0;
}

/* "[name]": 1; any other section header: -1; no header: 0 */
static int header( const char *line, const char *name )
{
	size_t n = strlen( name );
	char buf[40];
	while ( *line == ' ' || *line == '\t' )
		line++;
	if ( *line != '[' )
		return 0;
	sprintf( buf, "%.38s", line + 1 );
	return n < sizeof(buf) && buf[n] == ']' && ( buf[n] = 0, same( buf, name ) ) ? 1 : -1;
}

/* the value of "key=value" (key compared without case), else NULL */
static const char *value_of( const char *line, const char *key )
{
	size_t n = strlen( key );
	char buf[40];
	while ( *line == ' ' || *line == '\t' )
		line++;
	if ( n >= sizeof(buf) )
		return NULL;
	sprintf( buf, "%.*s", (int)n, line );
	if ( strlen( buf ) != n || !same( buf, key ) )
		return NULL;
	line += n;
	while ( *line == ' ' || *line == '\t' )
		line++;
	if ( *line != '=' )
		return NULL;
	line++;
	while ( *line == ' ' || *line == '\t' )
		line++;
	return line;
}

/* range of a section: first line after the header, end; -1 = none */
static int section( const char *name, int *end )
{
	int i, start = -1;
	for ( i = 0; i < nini; i++ ) {
		int h = header( ini[i], name );
		if ( start >= 0 && h ) {
			*end = i;
			return start;
		}
		if ( h == 1 )
			start = i + 1;
	}
	*end = nini;
	return start;
}

/* GroupN=: N, or 0 */
static int group_number( const char *line, const char **value )
{
	const char *p = line;
	int n = 0;
	while ( *p == ' ' || *p == '\t' )
		p++;
	if ( toupper( (unsigned char)p[0] ) != 'G' || toupper( (unsigned char)p[1] ) != 'R' ||
	     toupper( (unsigned char)p[2] ) != 'O' || toupper( (unsigned char)p[3] ) != 'U' ||
	     toupper( (unsigned char)p[4] ) != 'P' || !isdigit( (unsigned char)p[5] ) )
		return 0;
	p += 5;
	while ( isdigit( (unsigned char)*p ) )
		n = n * 10 + *p++ - '0';
	while ( *p == ' ' || *p == '\t' )
		p++;
	if ( *p != '=' )
		return 0;
	p++;
	while ( *p == ' ' || *p == '\t' )
		p++;
	*value = p;
	return n;
}

#ifdef __WATCOMC__
/* PROGMAN.INI is written in ANSI, DOS opens the files by their OEM names */
static void ansi_to_oem( char *text )
{
	unsigned char *p = (unsigned char *)text;
	unsigned i;
	for ( ; *p; p++ )
		if ( *p >= 0x80 )
			for ( i = 0; i < 128; i++ )
				if ( cp850[i] == *p ) {
					*p = (unsigned char)( 0x80 + i );
					break;
				}
}
#endif

static void group_path( char *out, const char *windir, const char *value )
{
	if ( ( value[0] && value[1] == ':' ) || value[0] == '/' )
		sprintf( out, "%.*s", GDI_PM_PATH - 1, value );
	else if ( value[0] == '\\' )
		sprintf( out, "%.2s%.*s", windir, GDI_PM_PATH - 3, value );
	else
		path_cat( out, windir, value );
#ifdef __WATCOMC__
	ansi_to_oem( out );
#endif
}

/* --- group files ------------------------------------------------------------ */

static int read_at( FILE *f, long at, unsigned char *buf, unsigned n )
{
	return fseek( f, at, SEEK_SET ) || fread( buf, 1, n, f ) != n ? -1 : 0;
}

static int read_string( FILE *f, long at, char *out, unsigned max )
{
	unsigned i;
	int c;
	if ( fseek( f, at, SEEK_SET ) )
		return -1;
	for ( i = 0; i + 1 < max; i++ ) {
		if ( ( c = getc( f ) ) == EOF )
			return -1;
		if ( !( out[i] = (char)c ) )
			return 0;
	}
	out[i] = 0;
	return 0;
}

static unsigned char ghdr[HDR];
static unsigned slot[MAXITEMS];
static unsigned nslot;

/* header and item table; -1 = not a group file */
static int read_table( FILE *f )
{
	unsigned char b[2];
	unsigned i;
	if ( read_at( f, 0, ghdr, HDR ) || memcmp( ghdr, "PMCC", 4 ) )
		return -1;
	nslot = get16( ghdr + 32 );
	if ( nslot > MAXITEMS )
		return -1;
	for ( i = 0; i < nslot; i++ ) {
		if ( fread( b, 1, 2, f ) != 2 )
			return -1;
		slot[i] = get16( b );
	}
	return 0;
}

static int group_name( const char *path, char *name, unsigned max )
{
	FILE *f = fopen( path, "rb" );
	int rc;
	if ( !f )
		return -1;
	rc = read_table( f ) ? -1 : read_string( f, get16( ghdr + 22 ), name, max );
	fclose( f );
	return rc;
}

/* the PROGMAN.INI line of the group; -1 = none.  Path and number set. */
static int find_group( const char *windir, const char *group, char *path, int *number )
{
	int i, end, start = section( "Groups", &end );
	char name[80];
	const char *v;
	if ( start < 0 )
		return -1;
	for ( i = start; i < end; i++ ) {
		int n = group_number( ini[i], &v );
		if ( !n )
			continue;
		group_path( path, windir, v );
		if ( !group_name( path, name, sizeof(name) ) && same( name, group ) ) {
			*number = n;
			return i;
		}
	}
	return -1;
}

/* the slot of the item with this title, -1 = none; f after read_table */
static int find_item( FILE *f, const char *title )
{
	unsigned char rec[ITEM];
	char name[80];
	unsigned i;
	for ( i = 0; i < nslot; i++ )
		if ( slot[i] && !read_at( f, slot[i], rec, ITEM ) &&
		     !read_string( f, get16( rec + 18 ), name, sizeof(name) ) &&
		     same( name, title ) )
			return (int)i;
	return -1;
}

/* sum of all words, so that the checksum (written as its negative) makes
 * the sum of the file zero */
static int fix_checksum( FILE *f )
{
	unsigned char buf[256], b[2];
	unsigned sum = 0, i;
	size_t n;
	put16( b, 0 );
	if ( fseek( f, 4, SEEK_SET ) || fwrite( b, 1, 2, f ) != 2 || fseek( f, 0, SEEK_SET ) )
		return -1;
	while ( ( n = fread( buf, 1, sizeof(buf), f ) ) > 0 ) {
		if ( n & 1 )
			buf[n++] = 0;
		for ( i = 0; i < n; i += 2 )
			sum += get16( buf + i );
	}
	put16( b, ( 0x10000L - ( sum & 0xFFFF ) ) & 0xFFFF );
	return fseek( f, 4, SEEK_SET ) || fwrite( b, 1, 2, f ) != 2 ? -1 : 0;
}

static int copy_range( FILE *in, long from, long to, FILE *out )
{
	unsigned char buf[256];
	size_t n;
	if ( fseek( in, from, SEEK_SET ) )
		return -1;
	while ( from < to ) {
		n = to - from > (long)sizeof(buf) ? sizeof(buf) : (size_t)( to - from );
		if ( fread( buf, 1, n, in ) != n || fwrite( buf, 1, n, out ) != n )
			return -1;
		from += n;
	}
	return 0;
}

static void temp_name( char *out, const char *path )
{
	char *dot, *slash;
	sprintf( out, "%.*s", GDI_PM_PATH - 5, path );
	dot = strrchr( out, '.' );
	slash = strrchr( out, '\\' );
	if ( !dot || ( slash && dot < slash ) )
		dot = out + strlen( out );
	strcpy( dot, ".$$$" );
}

static int replace_file( const char *temp, const char *path )
{
	remove( path );
	return rename( temp, path ) ? -1 : 0;
}

/* a free place for a new icon: the first grid cell without an item */
static void free_position( FILE *f, int *x, int *y )
{
	unsigned char rec[ITEM];
	unsigned i;
	int cell, used;
	for ( cell = 0; cell < MAXITEMS; cell++ ) {
		int cx = 34 + 75 * ( cell % COLUMNS ), cy = 2 + 70 * ( cell / COLUMNS );
		used = 0;
		for ( i = 0; i < nslot && !used; i++ )
			if ( slot[i] && !read_at( f, slot[i], rec, ITEM ) ) {
				int ix = (short)get16( rec ), iy = (short)get16( rec + 2 );
				if ( ix > cx - 38 && ix < cx + 38 && iy > cy - 35 && iy < cy + 35 )
					used = 1;
			}
		if ( !used ) {
			*x = cx;
			*y = cy;
			return;
		}
	}
	*x = 34;
	*y = 2;
}

/* rewrite the group with one more item; see the comment at the top */
static int append_item( const char *path, const char *title, const char *command,
                        const char *icon, int index )
{
	char temp[GDI_PM_PATH];
	unsigned char rec[ITEM], b[2], *p;
	FILE *in, *out;
	long cb, size, base, end;
	unsigned i, free_slot, delta, newcount, k;
	int x, y, rc = -1;
	if ( !( in = fopen( path, "rb" ) ) )
		return -1;
	if ( read_table( in ) || fseek( in, 0, SEEK_END ) ) {
		fclose( in );
		return -1;
	}
	size = ftell( in );
	cb = get16( ghdr + 6 );
	if ( cb < HDR + 2L * nslot || cb > size ) {
		fclose( in );
		return -1;
	}
	free_position( in, &x, &y );
	for ( free_slot = 0; free_slot < nslot && slot[free_slot]; free_slot++ );
	if ( free_slot == nslot && nslot == MAXITEMS ) {
		fclose( in );
		return -1;
	}
	delta = free_slot == nslot ? 2 : 0;
	newcount = nslot + ( delta ? 1 : 0 );
	base = cb + delta;   /* the new item */
	end = base + ITEM + strlen( title ) + 1 + strlen( command ) + 1 + strlen( icon ) + 1;
	if ( end + ( size - cb ) > 0xFFF0L ) {
		fclose( in );
		return -1;
	}
	temp_name( temp, path );
	if ( !( out = fopen( temp, "w+b" ) ) ) {
		fclose( in );
		return -1;
	}
	/* header: icon format 0 (Program Manager extracts the icons again) */
	put16( ghdr + 4, 0 );
	put16( ghdr + 6, (unsigned)end );
	put16( ghdr + 22, get16( ghdr + 22 ) + delta );
	put16( ghdr + 24, 0 );
	put16( ghdr + 26, 0 );
	put16( ghdr + 28, 0 );
	put16( ghdr + 32, newcount );
	if ( fwrite( ghdr, 1, HDR, out ) != HDR )
		goto done;
	for ( i = 0; i < newcount; i++ ) {
		k = i == free_slot ? (unsigned)base : i < nslot && slot[i] ? slot[i] + delta : 0;
		put16( b, k );
		if ( fwrite( b, 1, 2, out ) != 2 )
			goto done;
	}
	if ( copy_range( in, HDR + 2L * nslot, cb, out ) )
		goto done;
	/* the new item and its strings */
	memset( rec, 0, ITEM );
	put16( rec, (unsigned)x );
	put16( rec + 2, (unsigned)y );
	put16( rec + 4, (unsigned)index );
	k = (unsigned)base + ITEM;
	put16( rec + 18, k );
	k += strlen( title ) + 1;
	put16( rec + 20, k );
	k += strlen( command ) + 1;
	put16( rec + 22, k );
	if ( fwrite( rec, 1, ITEM, out ) != ITEM ||
	     fwrite( title, 1, strlen( title ) + 1, out ) != strlen( title ) + 1 ||
	     fwrite( command, 1, strlen( command ) + 1, out ) != strlen( command ) + 1 ||
	     fwrite( icon, 1, strlen( icon ) + 1, out ) != strlen( icon ) + 1 )
		goto done;
	/* tag data after the items (their item numbers stay the same) */
	if ( copy_range( in, cb, size, out ) )
		goto done;
	/* the offsets in the old items moved by delta */
	if ( delta )
		for ( i = 0; i < nslot; i++ ) {
			if ( !slot[i] )
				continue;
			if ( read_at( out, slot[i] + delta, rec, ITEM ) )
				goto done;
			for ( p = rec + 12; p < rec + ITEM; p += 2 )
				if ( get16( p ) )
					put16( p, get16( p ) + delta );
			if ( fseek( out, slot[i] + delta, SEEK_SET ) || fwrite( rec, 1, ITEM, out ) != ITEM )
				goto done;
		}
	if ( fix_checksum( out ) )
		goto done;
	rc = 0;
done:
	fclose( in );
	if ( fclose( out ) )
		rc = -1;
	if ( rc || replace_file( temp, path ) ) {
		remove( temp );
		return -1;
	}
	return 0;
}

/* a new, empty group file named after the group */
static int create_group( const char *windir, const char *group, char *path )
{
	char name[13];
	unsigned char h[HDR + 2];
	FILE *f;
	int n = 0, i, rc;
	unsigned off;
	for ( i = 0; group[i] && n < 8; i++ )
		if ( isalnum( (unsigned char)group[i] ) && (unsigned char)group[i] < 0x80 )
			name[n++] = (char)toupper( (unsigned char)group[i] );
	if ( !n )
		name[n++] = 'G';
	/* NAME.GRP, else NAME01.GRP, NAME02.GRP, ... (8 characters) */
	for ( i = 0; i < 100; i++ ) {
		if ( i )
			sprintf( name + ( n > 6 ? 6 : n ), "%02d.GRP", i );
		else
			strcpy( name + n, ".GRP" );
		path_cat( path, windir, name );
		if ( !( f = fopen( path, "rb" ) ) )
			break;
		fclose( f );
	}
	if ( i == 100 || !( f = fopen( path, "w+b" ) ) )
		return -1;
	memset( h, 0, sizeof(h) );
	memcpy( h, "PMCC", 4 );
	off = HDR + 2;
	put16( h + 6, off + strlen( group ) + 1 );
	put16( h + 8, 1 );            /* nCmdShow: normal */
	put16( h + 10, 66 );          /* rcNormal */
	put16( h + 12, 66 );
	put16( h + 14, 482 );
	put16( h + 16, 268 );
	put16( h + 18, 0xFFFF );      /* ptMin: none */
	put16( h + 20, 0xFFFF );
	put16( h + 22, off );         /* pName */
	put16( h + 32, 1 );           /* one free slot */
	rc = fwrite( h, 1, sizeof(h), f ) != sizeof(h) ||
	     fwrite( group, 1, strlen( group ) + 1, f ) != strlen( group ) + 1 ||
	     fix_checksum( f ) ? -1 : 0;
	if ( fclose( f ) )
		rc = -1;
	if ( rc )
		remove( path );
	return rc;
}

/* GroupN= and Order= for a new group file */
static int ini_add_group( const char *path )
{
	char line[GDI_PM_PATH + 20];
	const char *v;
	int i, end, start, n, used[MAXGROUP + 1], number;
	memset( used, 0, sizeof(used) );
	start = section( "Groups", &end );
	if ( start < 0 ) {
		if ( nini && ini[nini - 1][0] && ini_insert( nini, "" ) )
			return -1;
		if ( ini_insert( nini, "[Groups]" ) )
			return -1;
		start = end = nini;
	}
	for ( i = start; i < end; i++ )
		if ( ( n = group_number( ini[i], &v ) ) > 0 && n <= MAXGROUP )
			used[n] = 1;
	for ( number = 1; number <= MAXGROUP && used[number]; number++ );
	if ( number > MAXGROUP )
		return -1;
	while ( end > start && !ini[end - 1][0] )
		end--;
	sprintf( line, "Group%d=%s", number, path );
	if ( ini_insert( end, line ) )
		return -1;
	/* Order= in [Settings]: the new group comes last (in front) */
	start = section( "Settings", &end );
	for ( i = start; start >= 0 && i < end; i++ )
		if ( ( v = value_of( ini[i], "Order" ) ) != NULL ) {
			sprintf( line, "Order= %.*s %d", GDI_PM_PATH, v, number );
			ini_delete( i );
			return ini_insert( i, line );
		}
	return 0;
}

static void ini_remove_group( int at, int number )
{
	char line[GDI_PM_PATH + 20], out[GDI_PM_PATH + 20];
	const char *v;
	int i, end, start;
	ini_delete( at );
	start = section( "Settings", &end );
	for ( i = start; start >= 0 && i < end; i++ )
		if ( ( v = value_of( ini[i], "Order" ) ) != NULL ) {
			char *t;
			sprintf( line, "%.*s", GDI_PM_PATH, v );
			strcpy( out, "Order=" );   /* " 1 2 3" as Program Manager writes it */
			for ( t = strtok( line, " \t" ); t; t = strtok( NULL, " \t" ) )
				if ( atoi( t ) != number ) {
					strcat( out, " " );
					strcat( out, t );
				}
			ini_delete( i );
			ini_insert( i, out );
			return;
		}
}

int gdi_pm_add_item( const char *windir, const char *group, const char *title,
                     const char *command, const char *icon, int index,
                     int *created )
{
	char path[GDI_PM_PATH];
	FILE *f;
	int number, rc;
	*created = 0;
	if ( ini_read( windir ) )
		return -1;
	if ( find_group( windir, group, path, &number ) < 0 ) {
		if ( create_group( windir, group, path ) )
			goto error;
		if ( ini_add_group( path ) || ini_write( windir ) ) {
			remove( path );
			goto error;
		}
		*created = 1;
	} else {
		if ( !( f = fopen( path, "rb" ) ) )
			goto error;
		rc = read_table( f ) ? -2 : find_item( f, title );
		fclose( f );
		if ( rc == -2 )
			goto error;
		if ( rc >= 0 ) {
			ini_clear();
			return 1;
		}
	}
	ini_clear();
	return append_item( path, title, command, icon, index );
error:
	ini_clear();
	return -1;
}

int gdi_pm_remove_item( const char *windir, const char *group, const char *title )
{
	char path[GDI_PM_PATH];
	unsigned char b[2];
	FILE *f;
	int number, i, rc = 0;
	if ( ini_read( windir ) )
		return -1;
	i = find_group( windir, group, path, &number );
	ini_clear();
	if ( i < 0 )
		return 0;
	if ( !( f = fopen( path, "r+b" ) ) )
		return -1;
	if ( read_table( f ) )
		rc = -1;
	else if ( ( i = find_item( f, title ) ) >= 0 ) {
		put16( b, 0 );
		rc = fseek( f, HDR + 2L * i, SEEK_SET ) || fwrite( b, 1, 2, f ) != 2 ||
		     fix_checksum( f ) ? -1 : 0;
	}
	if ( fclose( f ) )
		rc = -1;
	return rc;
}

int gdi_pm_remove_group( const char *windir, const char *group )
{
	char path[GDI_PM_PATH];
	FILE *f;
	unsigned i;
	int number, at, rc;
	if ( ini_read( windir ) )
		return -1;
	if ( ( at = find_group( windir, group, path, &number ) ) < 0 ) {
		ini_clear();
		return 0;
	}
	if ( !( f = fopen( path, "rb" ) ) ) {
		ini_clear();
		return -1;
	}
	rc = read_table( f ) ? -1 : 0;
	fclose( f );
	for ( i = 0; !rc && i < nslot; i++ )
		if ( slot[i] )
			rc = 1;
	if ( !rc ) {
		ini_remove_group( at, number );
		rc = ini_write( windir ) ? -1 : 0;
		if ( !rc )
			remove( path );
	}
	ini_clear();
	return rc;
}
