/* sysfiles.c - GeosOne Dual Installer, CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI,
 * binary patches, update entries
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <io.h>
#include <dos.h>
#include <i86.h>
#include <process.h>
#include <setjmp.h>
#include <malloc.h>
#include "gdi.h"
#include "ui.h"
#include "hexpatch.h"
#include "regtxn.h"
#include "progman.h"
#include "core.h"

/* --- text file editing (CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI) -------- */

char *ed[MAXLINE];
int ned;

int ed_load( const char *path )
{
	FILE *f = fopen( path, "r" );
	char buf[256];
	ned = 0;
	if ( !f )
		return -1;
	while ( ned < MAXLINE && fgets( buf, sizeof(buf), f ) ) {
		int l = strlen( buf );
		while ( l && ( buf[l - 1] == '\n' || buf[l - 1] == '\r' || buf[l - 1] == 0x1A ) )
			buf[--l] = 0;
		ed[ned++] = str_dup( buf );
	}
	fclose( f );
	return 0;
}

void ed_insert( int at, const char *s )
{
	int i;
	if ( ned >= MAXLINE )
		return;
	for ( i = ned; i > at; i-- )
		ed[i] = ed[i - 1];
	ed[at] = str_dup( s );
	ned++;
}

static void ed_delete( int at )
{
	int i;
	free( ed[at] );
	for ( i = at; i < ned - 1; i++ )
		ed[i] = ed[i + 1];
	ned--;
}

/* keep a copy of the original: NAME.G01, NAME.G02, ...; its name goes to
 * saved (if not NULL), "" when there was nothing to keep or it failed */
void backup_as( const char *path, char *saved )
{
	char b[80], *dot;
	int i;
	if ( saved )
		*saved = 0;
	if ( !file_exists( path ) )
		return;
	strcpy( b, path );
	dot = strrchr( b, '.' );
	if ( !dot || strchr( dot, '\\' ) )
		dot = b + strlen( b );
	for ( i = 1; i < 100; i++ ) {
		sprintf( dot, ".G%02d", i );
		if ( !file_exists( b ) ) {
			if ( !copy_file( path, b ) && saved )
				strcpy( saved, b );
			return;
		}
	}
}

static void backup( const char *path )
{
	backup_as( path, NULL );
}

int ed_save( const char *path )
{
	FILE *f;
	int i;
	backup( path );
	if ( !( f = fopen( path, "w" ) ) )
		return -1;
	for ( i = 0; i < ned; i++ )
		fprintf( f, "%s\n", ed[i] );
	return fclose( f );
}

void ed_free( void )
{
	while ( ned )
		free( ed[--ned] );
}

/* remove the lines matching any Remove= of section; returns the count */
static int ed_remove( const char *section )
{
	const char *v;
	int pos = 0, i, n = 0;
	while ( ( pos = ini_next( section, "Remove", pos, &v ) ) >= 0 )
		for ( i = 0; i < ned; i++ )
			if ( contains( ed[i], v ) && ed[i][0] != '[' ) {
				ed_delete( i-- );
				n++;
			}
	return n;
}

/* Extend=cond|match|text|newline: text is appended to the lines that
 * contain match (e.g. " /D:ASPICD0" to the MSCDEX line; match may list
 * several texts: "MSCDEX,SHSUCDX"); without such a
 * line, newline is added like an Add= line.  Fields: m, t, n (expanded). */
int ext_split( const char *v, char *m, char *t, char *n )
{
	const char *b1 = strchr( v, '|' ), *b2;
	char tmp[256];
	if ( !b1 )
		return -1;
	sprintf( m, "%.*s", (int)( b1 - v ), v );
	b2 = strchr( b1 + 1, '|' );
	sprintf( tmp, "%.*s", b2 ? (int)( b2 - b1 - 1 ) : 255, b1 + 1 );
	expand( t, tmp, 256 );
	expand( n, b2 ? b2 + 1 : "", 256 );
	return 0;
}

/* contains one of the comma separated texts of list */
static int contains_any( const char *s, const char *list )
{
	char one[80];
	while ( *list ) {
		int n = 0;
		while ( list[n] && list[n] != ',' )
			n++;
		sprintf( one, "%.*s", n < 79 ? n : 79, list );
		if ( n && contains( s, one ) )
			return 1;
		list += n;
		if ( *list )
			list++;
	}
	return 0;
}

static int is_rem( const char *l )
{
	while ( *l == ' ' || *l == '@' )
		l++;
	return !strnicmp( l, "REM", 3 ) && ( !l[3] || l[3] == ' ' );
}

/* remove the first occurrence of pat (any case) from s */
static void strip( char *s, const char *pat )
{
	int l = strlen( pat );
	for ( ; *s; s++ )
		if ( !strnicmp( s, pat, l ) ) {
			memmove( s, s + l, strlen( s + l ) + 1 );
			return;
		}
}

/* PathAppend=cond|dir: dir in the PATH of AUTOEXEC.BAT, for the programs
 * of the package (USBSYNC, ASPITEST) from any directory; PathRemove=dir
 * takes dir out of it (a directory of an older version).  The value of a
 * line that sets the PATH ("PATH x", "PATH=x", "SET PATH=x"), or NULL */
static char *path_value( char *l )
{
	while ( *l == ' ' || *l == '\t' || *l == '@' )
		l++;
	if ( !strnicmp( l, "SET", 3 ) && ( l[3] == ' ' || l[3] == '\t' ) )
		for ( l += 3; *l == ' ' || *l == '\t'; l++ )
			;
	if ( strnicmp( l, "PATH", 4 ) || ( l[4] != ' ' && l[4] != '\t' && l[4] != '=' ) )
		return NULL;
	for ( l += 4; *l == ' ' || *l == '\t' || *l == '='; l++ )
		;
	return *l ? l : NULL;
}

/* the part of the PATH value v equal to dir (also with a "\" at the end),
 * or NULL; *len: its length */
static char *path_part( char *v, const char *dir, int *len )
{
	int n = strlen( dir );
	while ( n > 3 && dir[n - 1] == '\\' )
		n--;
	while ( *v ) {
		int l = 0;
		while ( v[l] && v[l] != ';' )
			l++;
		if ( ( l == n || ( l == n + 1 && v[n] == '\\' ) ) && !strnicmp( v, dir, n ) ) {
			*len = l;
			return v;
		}
		v += l;
		if ( *v )
			v++;
	}
	return NULL;
}

static void ed_add_path( void )
{
	const char *v, *t;
	char dir[80], line[256], *val;
	int pos = 0, i, last, n;
	while ( ( pos = ini_next( "Autoexec.bat", "PathAppend", pos, &v ) ) >= 0 ) {
		if ( !( t = cond_new( "Autoexec.bat", "PathAppend", v ) ) )
			continue;
		expand( dir, t, sizeof(dir) );
		/* the last line that sets the PATH counts */
		for ( last = -1, i = 0; i < ned; i++ )
			if ( !is_rem( ed[i] ) && path_value( ed[i] ) )
				last = i;
		if ( last < 0 ) {   /* none: the PATH of the system (Windows 9x) stays */
			sprintf( line, "PATH %%PATH%%;%s", dir );
			for ( i = 0; i < ned && !strnicmp( ed[i], "@ECHO", 5 ); i++ )
				;
			ed_insert( i, line );
			continue;
		}
		val = path_value( ed[last] );
		if ( path_part( val, dir, &n ) || strlen( ed[last] ) + strlen( dir ) + 2 > sizeof(line) )
			continue;
		sprintf( line, "%s%s%s", ed[last], val[strlen( val ) - 1] == ';' ? "" : ";", dir );
		free( ed[last] );
		ed[last] = str_dup( line );
	}
}

/* another package has its setup directory in dir (SetupDir=: packages
 * sharing a directory) and still needs dir in the PATH */
static int path_shared( const char *dir )
{
	struct find_t found;
	char p[80], q[80];
	path_cat( p, dir, "*.*" );
	if ( _dos_findfirst( p, _A_SUBDIR, &found ) )
		return 0;
	do {
		if ( !( found.attrib & _A_SUBDIR ) || found.name[0] == '.' )
			continue;
		path_cat( p, dir, found.name );
		path_cat( q, p, "INSTALL.INI" );
		if ( stricmp( p, jdir ) && file_exists( q ) )
			return 1;
	} while ( !_dos_findnext( &found ) );
	return 0;
}

/* dir out of every line that sets the PATH, the rest of the line stays; a
 * line left with nothing but %PATH% (added by PathAppend) goes */
static void path_drop( const char *dir )
{
	char *val, *part;
	int i, n;
	for ( i = 0; i < ned; i++ ) {
		if ( is_rem( ed[i] ) || !( val = path_value( ed[i] ) ) ||
			!( part = path_part( val, dir, &n ) ) )
			continue;
		/* with the ";" before it, or after it when it is the first */
		if ( part > val )
			memmove( part - 1, part + n, strlen( part + n ) + 1 );
		else
			memmove( part, part + n + ( part[n] == ';' ), strlen( part + n ) + 1 );
		if ( !stricmp( val, "%PATH%" ) )
			ed_delete( i-- );
		else if ( !*val )
			strcpy( val, ";" );   /* "PATH dir": an empty PATH, as without dir */
	}
}

/* PathRemove= (installation and INSTALL /U, like Remove=); INSTALL /U: the
 * directories of PathAppend= out again */
static void ed_unpath( void )
{
	const char *v, *t;
	char dir[80], list[256], *d;
	int pos = 0;
	while ( ( pos = ini_next( "Autoexec.bat", "PathRemove", pos, &v ) ) >= 0 ) {
		expand( list, v, sizeof(list) );   /* dir;dir;... */
		for ( d = strtok( list, ";" ); d; d = strtok( NULL, ";" ) ) {
			sprintf( dir, "%.79s", d );
			path_drop( dir );
		}
	}
	if ( !uninstall )
		return;
	for ( pos = 0; ( pos = ini_next( "Autoexec.bat", "PathAppend", pos, &v ) ) >= 0; ) {
		if ( !( t = cond( v ) ) )
			continue;
		expand( dir, t, sizeof(dir) );
		if ( !path_shared( dir ) )
			path_drop( dir );
	}
}

/* the program a DOS line starts (after @, LH or LOADHIGH and its switches):
 * the offset of its first character, and of the character after it */
static int line_program( const char *s, int *end )
{
	int a = 0, e;
	while ( s[a] == ' ' || s[a] == '\t' || s[a] == '@' )
		a++;
	if ( ( !strnicmp( s + a, "LH", 2 ) && ( s[a + 2] == ' ' || s[a + 2] == '\t' ) ) ||
		( !strnicmp( s + a, "LOADHIGH", 8 ) && ( s[a + 8] == ' ' || s[a + 8] == '\t' ) ) ) {
		while ( s[a] && s[a] != ' ' && s[a] != '\t' )
			a++;
		for ( ;; ) {   /* LH /L:1,2 /S */
			while ( s[a] == ' ' || s[a] == '\t' )
				a++;
			if ( s[a] != '/' )
				break;
			while ( s[a] && s[a] != ' ' && s[a] != '\t' )
				a++;
		}
	}
	for ( e = a; s[e] && s[e] != ' ' && s[e] != '\t' && s[e] != '/'; e++ )
		;
	*end = e;
	return a;
}

/* the name of the program at s (n characters) without path and extension
 * is one of the comma separated names of list */
static int program_is( const char *s, int n, const char *list )
{
	int b = n, x;
	while ( b > 0 && s[b - 1] != '\\' && s[b - 1] != ':' )
		b--;
	for ( x = b; x < n && s[x] != '.'; x++ )
		;
	while ( *list ) {
		int l = 0, m;
		while ( list[l] && list[l] != ',' )
			l++;
		for ( m = 0; m < l && list[m] != '.'; m++ )
			;
		if ( m && m == x - b && !strnicmp( s + b, list, m ) )
			return 1;
		list += l;
		if ( *list )
			list++;
	}
	return 0;
}

/* Program=condition|programs|new program: the lines that start one of the
 * programs start the new one (the rest of the line stays); the list may name
 * the new program too (another copy of it is replaced). AUTOEXEC.PRG
 * keeps "new|old" (old: from LH on) for INSTALL /U. */
static void ed_program( const char *section )
{
	const char *v, *t;
	char list[256], prog[256], line[256], journal[80];
	int pos = 0, i, a, e;
	FILE *f = NULL;
	path_cat( journal, jdir, "AUTOEXEC.PRG" );
	while ( ( pos = ini_next( section, "Program", pos, &v ) ) >= 0 ) {
		const char *b;
		if ( !( t = cond_new( section, "Program", v ) ) || !( b = strchr( t, '|' ) ) )
			continue;
		sprintf( list, "%.*s", (int)( b - t ), t );
		expand( prog, b + 1, sizeof(prog) );
		for ( i = 0; i < ned; i++ ) {
			a = line_program( ed[i], &e );
			if ( is_rem( ed[i] ) || !program_is( ed[i] + a, e - a, list ) ||
				( e - a == (int)strlen( prog ) && !strnicmp( ed[i] + a, prog, e - a ) ) ||
				strlen( ed[i] ) + strlen( prog ) >= sizeof(line) )
				continue;
			/* from LH on: LH SHSUCDX would relocate low, SHSUCDX goes high */
			for ( a = 0; ed[i][a] == ' ' || ed[i][a] == '\t' || ed[i][a] == '@'; a++ )
				;
			if ( !f && !( f = fopen( journal, "a" ) ) ) {
				char msg[120];
				sprintf( msg, "%s %s", T( "WriteError" ), journal );
				fail( msg );
			}
			fprintf( f, "%s|%.*s\n", prog, e - a, ed[i] + a );
			sprintf( line, "%.*s%s%s", a, ed[i], prog, ed[i] + e );
			free( ed[i] );
			ed[i] = str_dup( line );
		}
	}
	if ( f )
		fclose( f );
}

/* INSTALL /U: the programs Program= replaced come back */
static void ed_unprogram( void )
{
	char journal[80], l[256], line[256], *old;
	FILE *f;
	int i, a, e, n;
	path_cat( journal, jdir, "AUTOEXEC.PRG" );
	if ( !( f = fopen( journal, "r" ) ) )
		return;
	while ( fgets( l, sizeof(l), f ) ) {
		strip_eol( l );
		if ( !( old = strchr( l, '|' ) ) )
			continue;
		*old++ = 0;
		n = strlen( l );
		for ( i = 0; i < ned; i++ ) {
			a = line_program( ed[i], &e );
			if ( e - a == n && !strnicmp( ed[i] + a, l, n ) &&
				strlen( ed[i] ) + strlen( old ) < sizeof(line) ) {
				sprintf( line, "%.*s%s%s", a, ed[i], old, ed[i] + e );
				free( ed[i] );
				ed[i] = str_dup( line );
				break;
			}
		}
	}
	fclose( f );
	remove( journal );
}

/* INSTALL /U: take the text out again; a line added by Extend goes */
static void ed_unextend( const char *section )
{
	const char *v, *t;
	char m[256], x[256], n[256], line[256];
	int pos = 0, i;
	while ( ( pos = ini_next( section, "Extend", pos, &v ) ) >= 0 ) {
		if ( !( t = cond( v ) ) || ext_split( t, m, x, n ) || !*x )
			continue;
		strip( n, x );
		for ( i = 0; i < ned; i++ )
			if ( contains_any( ed[i], m ) && contains( ed[i], x ) && !is_rem( ed[i] ) ) {
				strcpy( line, ed[i] );
				strip( line, x );
				if ( *n && !stricmp( line, n ) )
					ed_delete( i-- );
				else {
					free( ed[i] );
					ed[i] = str_dup( line );
				}
			}
	}
}


/* update: the entries of the installed version (its INSTALL.INI) for the
 * system files and the groups.  An update adds only entries that are new in
 * this version; the old ones stay as they are (or as the user changed them),
 * and a Program Manager item the user deleted does not come back. */
#define MAXOLDENTRY 160
static char __far *old_entry[MAXOLDENTRY];
static int nold_entry;
static const char *const update_sections[] = { "Config.sys", "Autoexec.bat",
	"System.ini", "Win.ini", "ProgMan" };

/* the part of an entry that identifies it: [ProgMan] "group|title" (the
 * condition is the first field), else the whole value */
static int entry_id( const char *section, const char *v, char *id, unsigned size )
{
	const char *g, *e;
	if ( stricmp( section, "ProgMan" ) ) {
		sprintf( id, "%.*s", (int)size - 1, v );
		return 0;
	}
	if ( !( g = strchr( v, '|' ) ) || !( e = strchr( g + 1, '|' ) ) )
		return -1;
	e = strchr( e + 1, '|' );
	sprintf( id, "%.*s", e ? (int)( e - g - 1 ) : (int)size - 1, g + 1 );
	return 0;
}

void old_entries_load( const char *ini )
{
	FILE *f = fopen( ini, "r" );
	char l[256], id[256], *eq;
	const char *section = NULL;
	char __far *p;
	unsigned i;
	nold_entry = 0;
	if ( !f )
		return;
	while ( nold_entry < MAXOLDENTRY && fgets( l, sizeof(l), f ) ) {
		strip_eol( l );
		if ( *l == '[' ) {
			section = NULL;
			for ( i = 0; i < sizeof(update_sections) / sizeof(update_sections[0]); i++ )
				if ( !strnicmp( l + 1, update_sections[i], strlen( update_sections[i] ) ) &&
					l[1 + strlen( update_sections[i] )] == ']' )
					section = update_sections[i];
			continue;
		}
		if ( !section || *l == ';' || !( eq = strchr( l, '=' ) ) )
			continue;
		*eq = 0;
		if ( entry_id( section, eq + 1, id, sizeof(id) ) )
			continue;
		if ( ( p = _fmalloc( strlen( section ) + strlen( l ) + strlen( id ) + 3 ) ) == NULL )
			break;
		_fstrcpy( p, section );
		_fstrcat( p, "|" );
		_fstrcat( p, l );
		_fstrcat( p, "|" );
		_fstrcat( p, id );
		old_entry[nold_entry++] = p;
	}
	fclose( f );
}

/* is the entry key=v of section new (always, except an old one in an update) */
int entry_new( const char *section, const char *key, const char *v )
{
	char id[256], whole[300];
	int i;
	if ( !update_mode || entry_id( section, v, id, sizeof(id) ) )
		return 1;
	sprintf( whole, "%s|%s|%.250s", section, key, id );
	for ( i = 0; i < nold_entry; i++ )
		if ( !_fstricmp( old_entry[i], whole ) )
			return 0;
	return 1;
}

/* cond() of an entry that is to be applied now */
const char *cond_new( const char *section, const char *key, const char *v )
{
	return entry_new( section, key, v ) ? cond( v ) : NULL;
}

/* CONFIG.SYS / AUTOEXEC.BAT: lines of Add= (conditional) at the top or
 * bottom; with a [menu] (multi-config CONFIG.SYS) into the [common] block;
 * in AUTOEXEC.BAT before a line that starts Windows */
static void ed_add_dos( const char *section, int isconfig )
{
	const char *v, *t;
	char line[256];
	/* default: CONFIG.SYS at the end, AUTOEXEC.BAT at the top (it may end
	 * with a menu, a shell or Windows) */
	int pos = 0, at, top = !stricmp( ini_get( section, "Position", isconfig ? "bottom" : "top" ), "top" ), i;
	int menu = 0, common = -1;

	if ( !isconfig )
		ed_program( section );   /* before Extend= looks for the program */
	if ( isconfig ) {
		for ( i = 0; i < ned; i++ ) {
			if ( !strnicmp( ed[i], "[menu]", 6 ) )
				menu = 1;
			if ( !strnicmp( ed[i], "[common]", 8 ) )
				common = i;
		}
	}
	if ( menu ) {
		/* end of the last [common] block, or a new one at the end */
		if ( common < 0 ) {
			ed_insert( ned, "[common]" );
			at = ned;
		} else {
			for ( at = common + 1; at < ned && ed[at][0] != '['; at++ );
			if ( top )
				at = common + 1;
		}
	} else if ( top ) {
		at = 0;
		while ( at < ned && ( !strnicmp( ed[at], "@ECHO", 5 ) ) )
			at++;
	} else {
		at = ned;
		if ( !isconfig )   /* the program, not an argument (WIN C:\APP.EXE) */
			for ( i = 0; i < ned; i++ ) {
				int e, a = line_program( ed[i], &e );
				if ( !is_rem( ed[i] ) && program_is( ed[i] + a, e - a, "WIN" ) ) {
					at = i;
					break;
				}
			}
	}
	while ( ( pos = ini_next( section, "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond_new( section, "Add", v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			ed_insert( at++, line );
		}
	pos = 0;
	while ( ( pos = ini_next( section, "Extend", pos, &v ) ) >= 0 ) {
		char m[256], x[256], n[256];
		int found = 0;
		if ( !( t = cond_new( section, "Extend", v ) ) || ext_split( t, m, x, n ) )
			continue;
		for ( i = 0; i < ned; i++ )
			if ( contains_any( ed[i], m ) && !is_rem( ed[i] ) ) {
				found = 1;
				if ( !contains( ed[i], x ) && strlen( ed[i] ) + strlen( x ) < sizeof(line) ) {
					sprintf( line, "%s%s", ed[i], x );
					free( ed[i] );
					ed[i] = str_dup( line );
				}
			}
		if ( !found && *n )
			ed_insert( at++, n );
	}
	if ( !isconfig )
		ed_add_path();
}

/* the line of section (header "[name]") equal to text, or -1 */
static int ini_find_line( const char *hdr, const char *text )
{
	int i, in = 0;
	for ( i = 0; i < ned; i++ ) {
		if ( ed[i][0] == '[' )
			in = !strnicmp( ed[i], hdr, strlen( hdr ) );
		else if ( in && !stricmp( ed[i], text ) )
			return i;
	}
	return -1;
}

/* SYSTEM.INI: Add=cond|Section|line.  A line that is already there is not
 * added again (device=vshare.386, for example); the lines added are kept in
 * %DIR%\SYSTEM.ADD ("section|line"), and INSTALL /U removes just these. */
static void ed_add_ini( void )
{
	const char *v, *t;
	char sec[40], line[256], hdr[44], journal[80];
	int pos = 0, i;
	FILE *f = NULL;
	path_cat( journal, jdir, "SYSTEM.ADD" );
	while ( ( pos = ini_next( "System.ini", "Add", pos, &v ) ) >= 0 ) {
		const char *bar;
		if ( !( t = cond_new( "System.ini", "Add", v ) ) || !( bar = strchr( t, '|' ) ) )
			continue;
		sprintf( sec, "%.*s", (int)( bar - t ), t );
		expand( line, bar + 1, sizeof(line) );
		sprintf( hdr, "[%s]", sec );
		if ( ini_find_line( hdr, line ) >= 0 )
			continue;
		if ( !f && !( f = fopen( journal, "a" ) ) ) {
			char msg[120];
			sprintf( msg, "%s %s", T( "WriteError" ), journal );
			fail( msg );
		}
		fprintf( f, "%s|%s\n", sec, line );
		for ( i = 0; i < ned && strnicmp( ed[i], hdr, strlen( hdr ) ); i++ );
		if ( i == ned ) {
			ed_insert( ned, "" );
			ed_insert( ned, hdr );
			i = ned - 1;
		}
		/* at the end of the section (before empty lines and the next one) */
		for ( i++; i < ned && ed[i][0] != '['; i++ );
		while ( i > 0 && !ed[i - 1][0] )
			i--;
		ed_insert( i, line );
	}
	if ( f )
		fclose( f );
}

/* INSTALL /U: remove the lines of SYSTEM.ADD; 1 = there was a journal */
static int ed_unadd_ini( void )
{
	char journal[80], l[300], hdr[44], *bar;
	FILE *f;
	int i;
	path_cat( journal, jdir, "SYSTEM.ADD" );
	if ( !( f = fopen( journal, "r" ) ) )
		return 0;
	while ( fgets( l, sizeof(l), f ) ) {
		strip_eol( l );
		if ( !( bar = strchr( l, '|' ) ) )
			continue;
		*bar = 0;
		sprintf( hdr, "[%.40s]", l );
		if ( ( i = ini_find_line( hdr, bar + 1 ) ) >= 0 )
			ed_delete( i );
	}
	fclose( f );
	return 1;
}

/* SYSTEM.INI: Change=cond|Section|old line|new line replaces a line (for a
 * line that may be there more than once, device=*vtd for example).  The
 * lines replaced are kept in %DIR%\SYSTEM.CHG ("section|old|new"), and
 * INSTALL /U puts the old line back while the new one is still there. */
int change_split( const char *text, char *sec, char *old, char *line )
{
	const char *a = strchr( text, '|' ), *b = a ? strchr( a + 1, '|' ) : NULL;
	char raw[256];
	if ( !b || a == text || a - text > 39 || b - a - 1 < 1 || b - a - 1 > 255 || !b[1] )
		return -1;
	sprintf( sec, "%.*s", (int)( a - text ), text );
	sprintf( raw, "%.*s", (int)( b - a - 1 ), a + 1 );
	expand( old, raw, 256 );
	expand( line, b + 1, 256 );
	return 0;
}

static void ed_change_ini( void )
{
	const char *v, *t;
	char sec[40], old[256], line[256], hdr[44], journal[80];
	int pos = 0, i;
	FILE *f = NULL;
	path_cat( journal, jdir, "SYSTEM.CHG" );
	while ( ( pos = ini_next( "System.ini", "Change", pos, &v ) ) >= 0 ) {
		if ( !( t = cond_new( "System.ini", "Change", v ) ) )
			continue;
		if ( change_split( t, sec, old, line ) )
			fail( T( "ChangeDefinition" ) );
		sprintf( hdr, "[%s]", sec );
		if ( ( i = ini_find_line( hdr, old ) ) < 0 )
			continue;   /* not there (or changed already) */
		if ( !f && !( f = fopen( journal, "a" ) ) ) {
			char msg[120];
			sprintf( msg, "%s %s", T( "WriteError" ), journal );
			fail( msg );
		}
		fprintf( f, "%s|%s|%s\n", sec, old, line );
		free( ed[i] );
		ed[i] = str_dup( line );
	}
	if ( f )
		fclose( f );
}

/* INSTALL /U: the old lines of SYSTEM.CHG back; 1 = there was a journal */
static int ed_unchange_ini( void )
{
	char journal[80], l[560], sec[40], old[256], line[256], hdr[44];
	FILE *f;
	int i;
	path_cat( journal, jdir, "SYSTEM.CHG" );
	if ( !( f = fopen( journal, "r" ) ) )
		return 0;
	while ( fgets( l, sizeof(l), f ) ) {
		char *a, *b;
		strip_eol( l );
		if ( !( a = strchr( l, '|' ) ) || !( b = strchr( a + 1, '|' ) ) )
			continue;
		sprintf( sec, "%.*s", (int)( a - l ) < 39 ? (int)( a - l ) : 39, l );
		sprintf( old, "%.*s", (int)( b - a - 1 ) < 255 ? (int)( b - a - 1 ) : 255, a + 1 );
		sprintf( line, "%.255s", b + 1 );
		sprintf( hdr, "[%s]", sec );
		if ( ( i = ini_find_line( hdr, line ) ) >= 0 ) {
			free( ed[i] );
			ed[i] = str_dup( old );
		}
	}
	fclose( f );
	return 1;
}

/* SYSTEM.INI: Set=cond|Section|key=value.  Original values are kept in a
 * package-local journal, so uninstall can restore the exact previous line. */
static int ini_header_is( const char *line, const char *section )
{
	int length;
	while ( *line == ' ' || *line == '\t' )
		line++;
	if ( *line++ != '[' )
		return 0;
	length = strlen( section );
	return !strnicmp( line, section, length ) && line[length] == ']';
}

static int ini_is_header( const char *line )
{
	while ( *line == ' ' || *line == '\t' )
		line++;
	return *line == '[';
}

static int ini_key_is( const char *line, const char *key )
{
	int length;
	while ( *line == ' ' || *line == '\t' )
		line++;
	if ( *line == ';' || *line == '#' )
		return 0;
	length = strlen( key );
	if ( strnicmp( line, key, length ) )
		return 0;
	line += length;
	while ( *line == ' ' || *line == '\t' )
		line++;
	return *line == '=';
}

/* Return the matching key index, -1 if absent and -2 for duplicates.  End is
 * the insertion point at the end of an existing section, or -1 if absent. */
int ini_find_key( const char *section, const char *key, int *end )
{
	int i, inside = 0, found = -1;
	*end = -1;
	for ( i = 0; i < ned; i++ ) {
		if ( ini_is_header( ed[i] ) ) {
			if ( inside ) {
				*end = i;
				break;
			}
			inside = ini_header_is( ed[i], section );
			continue;
		}
		if ( inside && ini_key_is( ed[i], key ) ) {
			if ( found >= 0 )
				return -2;
			found = i;
		}
	}
	if ( inside && *end < 0 )
		*end = ned;
	return found;
}

int system_set_split( const char *text, char *section, char *key,
	char *line )
{
	const char *bar = strchr( text, '|' ), *equal;
	char value[256];
	int length;
	if ( !bar || !( equal = strchr( bar + 1, '=' ) ) )
		return -1;
	length = bar - text;
	if ( length < 1 || length > 39 )
		return -1;
	sprintf( section, "%.*s", length, text );
	length = equal - bar - 1;
	if ( length < 1 || length > 79 )
		return -1;
	sprintf( key, "%.*s", length, bar + 1 );
	expand( value, equal + 1, sizeof(value) );
	if ( strlen( key ) + strlen( value ) + 2 > 255 )
		return -1;
	sprintf( line, "%s=%s", key, value );
	return 0;
}

/* A repeated install may reuse its original journal, but only when the same
 * setting and installed value are described.  Never replace the saved
 * pre-install value with the value written by the first run. */
static int system_set_journal_matches( const char *path, const char *section,
	const char *key, const char *line )
{
	FILE *file = fopen( path, "r" );
	char header[40], old_section[40], old_key[80], flag[8];
	char oldline[256], newline[256];
	int matched = 0;
	if ( !file || !fgets( header, sizeof(header), file ) )
		goto error;
	strip_eol( header );
	if ( strcmp( header, "GDI-SYSTEM-SET 1" ) )
		goto error;
	while ( fgets( old_section, sizeof(old_section), file ) ) {
		if ( !fgets( old_key, sizeof(old_key), file ) ||
			!fgets( flag, sizeof(flag), file ) ||
			!fgets( oldline, sizeof(oldline), file ) ||
			!fgets( newline, sizeof(newline), file ) )
			goto error;
		strip_eol( old_section ); strip_eol( old_key ); strip_eol( flag );
		strip_eol( oldline ); strip_eol( newline );
		if ( strcmp( flag, "0" ) && strcmp( flag, "1" ) )
			goto error;
		if ( !stricmp( old_section, section ) && !stricmp( old_key, key ) ) {
			if ( strcmp( newline, line ) || matched )
				goto error;
			matched = 1;
		}
	}
	fclose( file );
	return matched;

error:
	if ( file )
		fclose( file );
	return -1;
}

/* journal of the Set= entries of an INI section: SYSTEM.SET, WIN.SET */
static const char *set_journal( const char *ini_section )
{
	return stricmp( ini_section, "Win.ini" ) ? "SYSTEM.SET" : "WIN.SET";
}

static int ed_set_ini( const char *ini_section )
{
	const char *value, *text;
	char journal[80], section[40], key[80], line[256], header[44];
	FILE *file = NULL;
	int pos = 0, index, end, present, reuse, record, appending = 0;
	int ansi = !stricmp( ini_section, "Win.ini" );
	path_cat( journal, jdir, set_journal( ini_section ) );
	reuse = file_exists( journal );
	while ( ( pos = ini_next( ini_section, "Set", pos, &value ) ) >= 0 ) {
		text = cond_new( ini_section, "Set", value );
		if ( !text )
			continue;
		if ( system_set_split( text, section, key, line ) )
			goto error;
		if ( ansi )   /* WIN.INI is written in the code page of Windows */
			gdi_pm_oem_to_ansi( line );
		index = ini_find_key( section, key, &end );
		if ( index == -2 )
			goto error;
		record = !reuse;
		if ( reuse ) {
			int matched = system_set_journal_matches( journal, section, key, line );
			if ( update_mode && matched == 0 ) {
				/* a value that is new in this version: one more record */
				if ( !file && !( file = fopen( journal, "a" ) ) )
					goto error;
				appending = record = 1;
			} else if ( update_mode && matched != 1 )
				continue;   /* the installed version set this key already */
			else if ( matched != 1 )
				goto error;
		} else if ( !file ) {
			if ( !( file = fopen( journal, "w" ) ) ||
				fprintf( file, "GDI-SYSTEM-SET 1\n" ) < 0 )
				goto error;
		}
		present = index >= 0;
		if ( record && fprintf( file, "%s\n%s\n%d\n%s\n%s\n", section, key,
			present, present ? ed[index] : "", line ) < 0 )
			goto error;
		if ( present ) {
			free( ed[index] );
			ed[index] = str_dup( line );
		} else if ( end >= 0 ) {
			while ( end > 0 && !ed[end - 1][0] )
				end--;
			ed_insert( end, line );
		} else {
			sprintf( header, "[%s]", section );
			if ( ned && ed[ned - 1][0] )
				ed_insert( ned, "" );
			ed_insert( ned, header );
			ed_insert( ned, line );
		}
	}
	return file && fclose( file ) ? -1 : 0;

error:
	if ( file ) {
		fclose( file );
		if ( !appending )
			remove( journal );
	}
	return -1;
}

/* removes [section] when it has no lines left (Set= created it) */
static void ed_drop_empty_section( const char *section )
{
	char header[44];
	int i, j;
	sprintf( header, "[%s]", section );
	for ( i = 0; i < ned && stricmp( ed[i], header ); i++ )
		;
	if ( i == ned )
		return;
	for ( j = i + 1; j < ned && ed[j][0] != '['; j++ )
		if ( ed[j][0] )
			return;
	while ( j > i + 1 )
		ed_delete( --j );
	ed_delete( i );
	if ( i > 0 && i == ned && !ed[i - 1][0] )
		ed_delete( i - 1 );
}

static int ed_restore_ini( char *journal, const char *ini_section )
{
	FILE *file;
	char header[40], section[40], key[80], flag[8], oldline[256], newline[256];
	int index, end, present;
	path_cat( journal, jdir, set_journal( ini_section ) );
	if ( !( file = fopen( journal, "r" ) ) )
		return 0;
	if ( !fgets( header, sizeof(header), file ) )
		goto error;
	strip_eol( header );
	if ( strcmp( header, "GDI-SYSTEM-SET 1" ) )
		goto error;
	while ( fgets( section, sizeof(section), file ) ) {
		if ( !fgets( key, sizeof(key), file ) ||
			!fgets( flag, sizeof(flag), file ) ||
			!fgets( oldline, sizeof(oldline), file ) ||
			!fgets( newline, sizeof(newline), file ) )
			goto error;
		strip_eol( section ); strip_eol( key ); strip_eol( flag );
		strip_eol( oldline ); strip_eol( newline );
		present = !strcmp( flag, "1" );
		if ( !present && strcmp( flag, "0" ) )
			goto error;
		index = ini_find_key( section, key, &end );
		if ( index == -2 )
			goto error;
		/* Preserve an administrator's later change instead of overwriting it. */
		if ( index >= 0 && !strcmp( ed[index], newline ) ) {
			if ( present ) {
				free( ed[index] );
				ed[index] = str_dup( oldline );
			} else {
				ed_delete( index );
				ed_drop_empty_section( section );
			}
		}
	}
	return fclose( file ) ? -1 : 1;

error:
	fclose( file );
	return -1;
}

static int has_active( const char *section, const char *key )
{
	const char *v;
	int pos = 0;
	while ( ( pos = ini_next( section, key, pos, &v ) ) >= 0 )
		if ( !strcmp( key, "Remove" ) || !strcmp( key, "PathRemove" ) ? !update_mode : cond_new( section, key, v ) != NULL )
			return 1;
	return 0;
}

int has_entry( const char *section, const char *key )
{
	const char *value;
	return ini_next( section, key, 0, &value ) >= 0;
}

/* [HexPatch] Patch=cond|file|old bytes|new bytes|exact count.  The
 * condition is optional, as for other installer entries. */
char *trim_field( char *text )
{
	char *end;
	while ( *text == ' ' || *text == '\t' )
		text++;
	end = text + strlen( text );
	while ( end > text && ( end[-1] == ' ' || end[-1] == '\t' ) )
		*--end = 0;
	return text;
}

int patch_split( const char *value, char *path, char *old_hex,
	char *new_hex, int *expected )
{
	char data[256], *field[4], *bar, *end;
	int i;
	if ( strlen( value ) >= sizeof(data) )
		return -1;
	strcpy( data, value );
	field[0] = data;
	for ( i = 0; i < 3; i++ ) {
		bar = strchr( field[i], '|' );
		if ( !bar )
			return -1;
		*bar = 0;
		field[i + 1] = bar + 1;
	}
	if ( strchr( field[3], '|' ) )
		return -1;
	for ( i = 0; i < 4; i++ )
		field[i] = trim_field( field[i] );
	if ( !*field[0] || !*field[1] || !*field[2] || !*field[3] )
		return -1;
	expand( path, field[0], 80 );
	sprintf( old_hex, "%.95s", field[1] );
	sprintf( new_hex, "%.95s", field[2] );
	*expected = (int)strtol( field[3], &end, 10 );
	while ( *end == ' ' || *end == '\t' )
		end++;
	return *end || *expected < 1 || *expected > 32 ? -1 : 0;
}

#define MAX_PATCH_VARIANTS 16


/* PatchVariant=group|label|file|old bytes|new bytes|exact count.
 * Entries sharing a group are alternatives; their exact signatures select
 * the language/build without trusting a localized version string. */
int patch_variant_split( const char *value, PatchVariant *variant )
{
	char data[256], *field[6], *bar, *end;
	int i, old_length, new_length;
	if ( strlen( value ) >= sizeof(data) )
		return -1;
	strcpy( data, value );
	field[0] = data;
	for ( i = 0; i < 5; i++ ) {
		bar = strchr( field[i], '|' );
		if ( !bar )
			return -1;
		*bar = 0;
		field[i + 1] = bar + 1;
	}
	if ( strchr( field[5], '|' ) )
		return -1;
	for ( i = 0; i < 6; i++ )
		field[i] = trim_field( field[i] );
	for ( i = 0; i < 6; i++ )
		if ( !*field[i] )
			return -1;
	sprintf( variant->group, "%.23s", field[0] );
	sprintf( variant->label, "%.23s", field[1] );
	expand( variant->path, field[2], sizeof(variant->path) );
	old_length = hexpatch_parse( field[3], variant->before,
		sizeof(variant->before) );
	new_length = hexpatch_parse( field[4], variant->after,
		sizeof(variant->after) );
	variant->expected = (int)strtol( field[5], &end, 10 );
	while ( *end == ' ' || *end == '\t' )
		end++;
	if ( *end || old_length < 1 || old_length != new_length ||
		variant->expected < 1 || variant->expected > 32 )
		return -1;
	variant->length = old_length;
	variant->processed = 0;
	return 0;
}

int hexpatch_needs_windows( void )
{
	static const char *keys[] = { "Patch", "PatchIfPresent", "PatchVariant" };
	const char *value;
	int key, pos;
	for ( key = 0; key < 3; key++ )
		for ( pos = 0; ( pos = ini_next( "HexPatch", keys[key], pos,
			&value ) ) >= 0; )
			if ( contains( value, "%WINDIR%" ) )
				return 1;
	return 0;
}

static int patch_one( const char *path, const unsigned char *before,
	const unsigned char *after, int length, int expected );

static void patch_variants( void )
{
	static PatchVariant variants[MAX_PATCH_VARIANTS];
	const char *value, *text;
	char message[180];
	int count = 0, pos = 0, i, j, selected, applicable, result, io_error;
	int any_existing;

	while ( ( pos = ini_next( "HexPatch", "PatchVariant", pos, &value ) ) >= 0 ) {
		text = cond( value );
		if ( !text )
			continue;
		if ( count == MAX_PATCH_VARIANTS ||
			patch_variant_split( text, &variants[count] ) )
			fail( T( "PatchDefinition" ) );
		if ( uninstall ) {
			unsigned char swap[HEXPATCH_MAX_BYTES];
			memcpy( swap, variants[count].before, variants[count].length );
			memcpy( variants[count].before, variants[count].after,
				variants[count].length );
			memcpy( variants[count].after, swap, variants[count].length );
		}
		count++;
	}
	for ( i = 0; i < count; i++ ) {
		if ( variants[i].processed )
			continue;
		selected = -1;
		applicable = 0;
		io_error = 0;
		any_existing = 0;
		for ( j = i; j < count; j++ )
			if ( !stricmp( variants[i].group, variants[j].group ) ) {
				variants[j].processed = 1;
				if ( uninstall && !file_exists( variants[j].path ) )
					continue;
				any_existing = 1;
				result = hexpatch_probe( variants[j].path, variants[j].before,
					variants[j].after, variants[j].length,
					variants[j].expected );
				if ( result == HEXPATCH_OK || result == HEXPATCH_ALREADY ) {
					selected = j;
					applicable++;
				} else if ( result == HEXPATCH_IO )
					io_error = 1;
			}
		if ( uninstall && selected < 0 && !io_error && !any_existing )
			continue;
		if ( applicable != 1 ) {
			if ( io_error && !applicable )
				sprintf( message, "%s %s", T( "PatchError" ),
					variants[i].path );
			else
				sprintf( message, "%s %s", T( "PatchMismatch" ),
					variants[i].path );
			fail( message );
		}
		result = patch_one( variants[selected].path,
			variants[selected].before, variants[selected].after,
			variants[selected].length, variants[selected].expected );
		if ( result != HEXPATCH_OK && result != HEXPATCH_ALREADY ) {
			sprintf( message, "%s %s", T( "PatchError" ),
				variants[selected].path );
			fail( message );
		}
	}
}

/* a patch at installation: its backup goes to INSTALL.LOG, so that
 * INSTALL /U removes it after the checked reverse patch (which keeps no
 * backup) */
static int patch_one( const char *path, const unsigned char *before,
	const unsigned char *after, int length, int expected )
{
	char saved[80], p[80];
	FILE *f;
	int result = hexpatch_file_ex( path, before, after, length, expected,
		saved, !uninstall );
	if ( result == HEXPATCH_OK && !uninstall && *saved ) {
		path_cat( p, jdir, "INSTALL.LOG" );
		if ( ( f = fopen( p, "a" ) ) != NULL ) {
			fprintf( f, "%s\n", saved );
			fclose( f );
		}
	}
	return result;
}

void patch_all( void )
{
	static const char *keys[] = { "Patch", "PatchIfPresent" };
	const char *value, *text;
	unsigned char old_bytes[HEXPATCH_MAX_BYTES], new_bytes[HEXPATCH_MAX_BYTES];
	char path[80], old_hex[96], new_hex[96], message[180];
	int key, pos, expected, old_length, new_length, result;

	for ( key = 0; key < 2; key++ )
		for ( pos = 0; ( pos = ini_next( "HexPatch", keys[key], pos,
			&value ) ) >= 0; ) {
			text = cond( value );
			if ( !text )
				continue;
			if ( patch_split( text, path, old_hex, new_hex, &expected ) )
				fail( T( "PatchDefinition" ) );
			old_length = hexpatch_parse( old_hex, old_bytes, sizeof(old_bytes) );
			new_length = hexpatch_parse( new_hex, new_bytes, sizeof(new_bytes) );
			if ( old_length < 1 || old_length != new_length )
				fail( T( "PatchDefinition" ) );
			if ( !file_exists( path ) && ( key == 1 || uninstall ) )
				continue;
			if ( uninstall ) {
				unsigned char swap[HEXPATCH_MAX_BYTES];
				memcpy( swap, old_bytes, old_length );
				memcpy( old_bytes, new_bytes, old_length );
				memcpy( new_bytes, swap, old_length );
			}
			result = patch_one( path, old_bytes, new_bytes, old_length,
				expected );
			if ( result == HEXPATCH_OK || result == HEXPATCH_ALREADY )
				continue;
			if ( result == HEXPATCH_MISMATCH )
				sprintf( message, "%s %s", T( "PatchMismatch" ), path );
			else
				sprintf( message, "%s %s", T( "PatchError" ), path );
			fail( message );
		}
	patch_variants();
}

/* --- [Ini] Set=file|section|key=value: any INI file ----------------------
 * The file is a full path or a name in the Windows directory.  INI.SET in
 * %DIR% keeps per value the file, the section, the key, whether the key was
 * there, its previous line and the line written; INSTALL /U puts the
 * previous line back while the line is still as Setup left it. */

/* "file|section|key=value": the full path of the file and the rest */
int ini_set_file( const char *text, char *path, const char **rest )
{
	const char *bar = strchr( text, '|' );
	char name[80];
	if ( !bar || bar == text || bar - text > 79 )
		return -1;
	sprintf( name, "%.*s", (int)( bar - text ), text );
	expand( path, name, 80 );
	if ( path[1] != ':' ) {
		if ( !windir[0] || strlen( windir ) + strlen( path ) + 2 > 80 )
			return -1;
		strcpy( name, path );
		path_cat( path, windir, name );
	}
	*rest = bar + 1;
	return 0;
}

/* the journal has a record of path [section] key */
static int ini_journal_has( const char *journal, const char *path,
	const char *section, const char *key )
{
	FILE *f = fopen( journal, "r" );
	char l[6][256];
	int i, found = 0;
	if ( !f )
		return 0;
	fgets( l[0], sizeof(l[0]), f );
	while ( !found ) {
		for ( i = 0; i < 6 && fgets( l[i], sizeof(l[i]), f ); i++ )
			strip_eol( l[i] );
		if ( i < 6 )
			break;
		found = !stricmp( l[0], path ) && !stricmp( l[1], section ) && !stricmp( l[2], key );
	}
	fclose( f );
	return found;
}

/* the file being edited (ed) is saved and closed; INSTALL /U removes a
 * file nothing is left in (Setup created it) */
static void ini_file_done( char *loaded, int changed )
{
	if ( *loaded && changed && uninstall && !ned ) {
		remove( loaded );
		changed = 0;
	}
	if ( *loaded && changed && ed_save( loaded ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "WriteError" ), loaded );
		fail( msg );
	}
	if ( *loaded )
		ed_free();
	*loaded = 0;
}

static void ini_set_all( void )
{
	char journal[80], path[80], loaded[80], section[40], key[80], line[256], header[44];
	const char *v, *t, *rest;
	FILE *file = NULL;
	int pos = 0, index, end, changed = 0;
	path_cat( journal, jdir, "INI.SET" );
	*loaded = 0;
	while ( ( pos = ini_next( "Ini", "Set", pos, &v ) ) >= 0 ) {
		if ( !( t = cond_new( "Ini", "Set", v ) ) )
			continue;
		if ( ini_set_file( t, path, &rest ) || system_set_split( rest, section, key, line ) )
			fail( T( "IniDefinition" ) );
		if ( ini_journal_has( journal, path, section, key ) )
			continue;   /* set by the installed version: its old value stays */
		if ( stricmp( path, loaded ) ) {
			ini_file_done( loaded, changed );
			profile_flush( path );
			ed_load( path );   /* a new file when there is none */
			strcpy( loaded, path );
			changed = 0;
		}
		if ( windir[0] && !strnicmp( path, windir, strlen( windir ) ) )
			gdi_pm_oem_to_ansi( line );   /* Windows INI files are ANSI */
		if ( ( index = ini_find_key( section, key, &end ) ) == -2 )
			fail( T( "IniDefinition" ) );
		if ( !file ) {
			int fresh = !file_exists( journal );
			if ( !( file = fopen( journal, "a" ) ) ||
				( fresh && fprintf( file, "GDI-INI-SET 1\n" ) < 0 ) )
				fail( T( "IniDefinition" ) );
		}
		fprintf( file, "%s\n%s\n%s\n%d\n%s\n%s\n", path, section, key,
			index >= 0, index >= 0 ? ed[index] : "", line );
		if ( index >= 0 ) {
			free( ed[index] );
			ed[index] = str_dup( line );
		} else if ( end >= 0 ) {
			while ( end > 0 && !ed[end - 1][0] )
				end--;
			ed_insert( end, line );
		} else {
			sprintf( header, "[%s]", section );
			if ( ned && ed[ned - 1][0] )
				ed_insert( ned, "" );
			ed_insert( ned, header );
			ed_insert( ned, line );
		}
		changed = 1;
	}
	ini_file_done( loaded, changed );
	if ( file && fclose( file ) )
		fail( T( "IniDefinition" ) );
}

/* INSTALL /U: the values of INI.SET back */
static void ini_restore_all( void )
{
	static char journal[80], loaded[80];
	char (*l)[256];   /* 6 lines, on the heap: the data segment is full */
	FILE *f;
	int i, index, end, changed = 0;
	path_cat( journal, jdir, "INI.SET" );
	if ( !( f = fopen( journal, "r" ) ) )
		return;
	if ( !( l = malloc( 6 * sizeof(*l) ) ) )
		gdi_out_of_memory();
	*loaded = 0;
	if ( !fgets( l[0], sizeof(l[0]), f ) || strncmp( l[0], "GDI-INI-SET 1", 13 ) ) {
		fclose( f );
		fail( T( "SetRestoreError" ) );
	}
	for ( ;; ) {
		for ( i = 0; i < 6 && fgets( l[i], sizeof(l[i]), f ); i++ )
			strip_eol( l[i] );
		if ( i < 6 )
			break;
		if ( stricmp( l[0], loaded ) ) {
			ini_file_done( loaded, changed );
			changed = 0;
			profile_flush( l[0] );
			if ( ed_load( l[0] ) )
				continue;   /* the file is gone */
			strcpy( loaded, l[0] );
		}
		index = ini_find_key( l[1], l[2], &end );
		/* a value changed after the installation stays */
		if ( index < 0 || strcmp( ed[index], l[5] ) )
			continue;
		if ( l[3][0] == '1' ) {
			free( ed[index] );
			ed[index] = str_dup( l[4] );
		} else {
			ed_delete( index );
			ed_drop_empty_section( l[1] );
		}
		changed = 1;
	}
	fclose( f );
	free( l );
	ini_file_done( loaded, changed );
	remove( journal );
}

void ini_files( void )
{
	if ( uninstall )
		ini_restore_all();
	else
		ini_set_all();
}

/* kind: 0 CONFIG.SYS, 1 AUTOEXEC.BAT, 2 SYSTEM.INI, 3 WIN.INI */
void edit_file( const char *path, const char *section, int kind )
{
	char set_journal_path[80];
	int restored = 0, set_journal_exists = 0, unadded = 0, unchanged = 0;
	if ( kind >= 2 && uninstall ) {
		path_cat( set_journal_path, jdir, set_journal( section ) );
		set_journal_exists = file_exists( set_journal_path );
		if ( kind == 2 ) {
			char added[80];
			path_cat( added, jdir, "SYSTEM.ADD" );
			set_journal_exists |= file_exists( added );
			path_cat( added, jdir, "SYSTEM.CHG" );
			set_journal_exists |= file_exists( added );
		}
	}
	if ( !has_active( section, "Add" ) && !has_active( section, "Remove" ) &&
		!has_active( section, "Extend" ) && !has_active( section, "PathAppend" ) &&
		!has_active( section, "PathRemove" ) && !has_active( section, "Program" ) &&
		!( kind == 2 && has_active( section, "Change" ) ) &&
		!( kind >= 2 && ( has_active( section, "Set" ) ||
			set_journal_exists ) ) )
		return;
	if ( ed_load( path ) && uninstall )
		return;
	if ( !uninstall && !update_mode && !stricmp( ini_get( section, "Replace", "no" ), "yes" ) )
		ed_free();   /* a new file (the old one is kept as backup) */
	if ( kind >= 2 && uninstall ) {
		restored = ed_restore_ini( set_journal_path, section );
		if ( restored < 0 )
			fail( T( "SetRestoreError" ) );
		if ( kind == 2 ) {
			unadded = ed_unadd_ini();
			unchanged = ed_unchange_ini();
		}
	}
	if ( !update_mode ) {   /* an update only adds the new entries */
		ed_remove( section );
		if ( kind < 2 )
			ed_unextend( section );
		if ( kind == 1 )
			ed_unpath();
		if ( kind == 1 && uninstall )
			ed_unprogram();
	}
	if ( !uninstall ) {
		if ( kind == 2 ) {
			ed_add_ini();
			ed_change_ini();
		}
		else if ( kind < 2 )
			ed_add_dos( section, kind == 0 );
	}
	if ( kind >= 2 && !uninstall && ed_set_ini( section ) )
		fail( T( "SetDefinition" ) );
	if ( ed_save( path ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "WriteError" ), path );
		fail( msg );
	}
	if ( restored > 0 )
		remove( set_journal_path );
	if ( unadded ) {
		path_cat( set_journal_path, jdir, "SYSTEM.ADD" );
		remove( set_journal_path );
	}
	if ( unchanged ) {
		path_cat( set_journal_path, jdir, "SYSTEM.CHG" );
		remove( set_journal_path );
	}
	ed_free();
}
