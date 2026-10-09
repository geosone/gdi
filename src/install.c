/* install.c - GeosOne DOS Installer (INSTALL.EXE): installs a DOS program
 * or driver as described by INSTALL.INI (next to INSTALL.EXE):
 * welcome text, license to accept, target directory, options, files to
 * copy, lines to add to / remove from CONFIG.SYS, AUTOEXEC.BAT and the
 * SYSTEM.INI of Windows 3.x.  INSTALL /U removes the installation again
 * (run from the target directory).  The files may come from GDA archives
 * on several disks (archive.c).  In DOS mode ([Setup] Mode=DOS) it also
 * prepares the hard disk (FDISK, FORMAT) and installs DOS itself, then
 * offers further installations (other INSTALL.INI).  See README.md.
 *
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
#include "gdi.h"

#define MAXOPT   16
#define MAXLINE  600
#define MAXFIND  8

static char srcdir[80], dest[80], windir[80], boot[3] = "C:";
static char opt_on[MAXOPT];
static const char *opt_label[MAXOPT];
static char opt_value[MAXOPT][24];   /* %On%: value when option n is on */
static int nopt;
static int uninstall, dosmode, bootset;
static struct {
	char name[16];
	char path[80];
	char file[13];   /* found on the installation disk: copied to %DIR% */
} finds[MAXFIND];   /* [Find] */
static int nfind;
static FILE *logf;
static const char *gdi_self;           /* argv[0] */
static jmp_buf extra_abort;            /* an extra installation was cancelled */
static int in_extra;                   /* INSTALL.LOG: installed files */

/* text of the installer, replaced by [Text] key=...; "\n" = new line */
static const char *T( const char *key, const char *def )
{
	char *v = (char *)ini_get( "Text", key, NULL ), *p;
	if ( !v )
		return def;
	while ( ( p = strstr( v, "\\n" ) ) != NULL ) {
		*p = '\n';
		memmove( p + 1, p + 2, strlen( p + 2 ) + 1 );
	}
	return v;
}

/* --- helpers -------------------------------------------------------- */

static void path_cat( char *dst, const char *dir, const char *name )
{
	strcpy( dst, dir );
	if ( *dst && dst[strlen( dst ) - 1] != '\\' )
		strcat( dst, "\\" );
	strcat( dst, name );
}

static int file_exists( const char *p )
{
	return access( p, 0 ) == 0;
}

/* %DIR%, %WINDIR%, %BOOT%, %SRC% */
static void expand( char *out, const char *in, int max )
{
	int o = 0;
	while ( *in && o < max - 1 ) {
		const char *rep = NULL;
		int l = 0;
		if ( !strnicmp( in, "%DIR%", 5 ) ) { rep = dest; l = 5; }
		else if ( !strnicmp( in, "%WINDIR%", 8 ) ) { rep = windir; l = 8; }
		else if ( !strnicmp( in, "%BOOT%", 6 ) ) { rep = boot; l = 6; }
		else if ( !strnicmp( in, "%SRC%", 5 ) ) { rep = srcdir; l = 5; }
		else if ( in[0] == '%' && !( ( in[1] == 'O' || in[1] == 'o' ) && isdigit( in[2] ) ) ) {   /* [Find] variables */
			int i;
			for ( i = 0; i < nfind; i++ ) {
				int n = strlen( finds[i].name );
				if ( !strnicmp( in + 1, finds[i].name, n ) && in[n + 1] == '%' ) {
					rep = finds[i].path;
					l = n + 2;
					break;
				}
			}
		}
		else if ( in[0] == '%' && ( in[1] == 'O' || in[1] == 'o' ) && isdigit( in[2] ) ) {
			int n = atoi( in + 2 ), d = 2;
			while ( isdigit( in[d] ) )
				d++;
			if ( in[d] == '%' && n >= 1 && n <= nopt ) {
				rep = opt_on[n - 1] ? opt_value[n - 1] : "";
				l = d + 1;
			}
		}
		if ( rep ) {
			while ( *rep && o < max - 1 )
				out[o++] = *rep++;
			in += l;
		} else
			out[o++] = *in++;
	}
	out[o] = 0;
}

static const char *find_path( const char *name, int n )
{
	int i;
	for ( i = 0; i < nfind; i++ )
		if ( (int)strlen( finds[i].name ) == n && !strnicmp( finds[i].name, name, n ) )
			return finds[i].path;
	return "";
}

/* "1,!2,?CDEX|text": options 1 on and 2 off, [Find] CDEX found; returns
 * the text or NULL */
static const char *cond( const char *v )
{
	const char *bar = strchr( v, '|' ), *p = v;
	if ( !bar )
		return v;
	while ( p < bar ) {
		int neg = 0, n;
		while ( *p == ' ' || *p == ',' )
			p++;
		if ( p >= bar )
			break;
		if ( *p == '!' ) {
			neg = 1;
			p++;
		}
		if ( *p == '?' ) {
			const char *q = ++p;
			while ( p < bar && *p != ',' && *p != ' ' )
				p++;
			if ( ( *find_path( q, p - q ) != 0 ) == neg )
				return NULL;
			continue;
		}
		if ( !isdigit( *p ) )   /* not a condition: plain text with '|' */
			return v;
		n = atoi( p );
		while ( p < bar && isdigit( *p ) )
			p++;
		if ( n >= 1 && n <= nopt && ( opt_on[n - 1] != 0 ) == neg )
			return NULL;
	}
	return bar + 1;
}

static int contains( const char *s, const char *pat )
{
	int l = strlen( pat );
	for ( ; *s; s++ )
		if ( !strnicmp( s, pat, l ) )
			return 1;
	return 0;
}

static int mkdirs( const char *path )
{
	char p[80];
	int i;
	strcpy( p, path );
	for ( i = 3; p[i]; i++ )
		if ( p[i] == '\\' ) {
			p[i] = 0;
			mkdir( p );
			p[i] = '\\';
		}
	mkdir( p );
	return file_exists( p ) ? 0 : -1;
}

static int copy_file( const char *from, const char *to )
{
	FILE *in = fopen( from, "rb" ), *out;
	static char buf[4096];
	size_t n;
	if ( !in )
		return -1;
	if ( !( out = fopen( to, "wb" ) ) ) {
		fclose( in );
		return -1;
	}
	while ( ( n = fread( buf, 1, sizeof(buf), in ) ) > 0 )
		if ( fwrite( buf, 1, n, out ) != n ) {
			fclose( in );
			fclose( out );
			return -1;
		}
	fclose( in );
	return fclose( out ) ? -1 : 0;
}

static void fail( const char *text )
{
	static const char *ok[] = { "OK" };
	scr_message( T( "ErrorTitle", "Error" ), text, ok, 1, 0, 1 );
	if ( in_extra )
		longjmp( extra_abort, 2 );
	scr_exit();
	exit( 2 );
}

static void abort_setup( void )
{
	static const char *ok[] = { "OK" };
	scr_message( T( "AbortTitle", "Setup" ), T( "Aborted", "Setup was cancelled. Nothing was changed." ), ok, 1, 0, 0 );
	if ( in_extra )
		longjmp( extra_abort, 1 );
	scr_exit();
	exit( 1 );
}

/* --- text file editing (CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI) -------- */

static char *ed[MAXLINE];
static int ned;

static int ed_load( const char *path )
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

static void ed_insert( int at, const char *s )
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

/* keep a copy of the original: NAME.G01, NAME.G02, ... */
static void backup( const char *path )
{
	char b[80], *dot;
	int i;
	if ( !file_exists( path ) )
		return;
	strcpy( b, path );
	dot = strrchr( b, '.' );
	if ( !dot || strchr( dot, '\\' ) )
		dot = b + strlen( b );
	for ( i = 1; i < 100; i++ ) {
		sprintf( dot, ".G%02d", i );
		if ( !file_exists( b ) ) {
			copy_file( path, b );
			return;
		}
	}
}

static int ed_save( const char *path )
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

static void ed_free( void )
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
static int ext_split( const char *v, char *m, char *t, char *n )
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
		if ( !isconfig )
			for ( i = 0; i < ned; i++ ) {
				const char *s = ed[i], *b;
				while ( *s == ' ' || *s == '@' )
					s++;
				b = strrchr( s, '\\' );
				b = b ? b + 1 : s;
				if ( ( !strnicmp( b, "WIN", 3 ) && ( !b[3] || b[3] == ' ' || b[3] == '.' ) ) ) {
					at = i;
					break;
				}
			}
	}
	while ( ( pos = ini_next( section, "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			ed_insert( at++, line );
		}
	pos = 0;
	while ( ( pos = ini_next( section, "Extend", pos, &v ) ) >= 0 ) {
		char m[256], x[256], n[256];
		int found = 0;
		if ( !( t = cond( v ) ) || ext_split( t, m, x, n ) )
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
}

/* SYSTEM.INI: Add=cond|Section|line */
static void ed_add_ini( void )
{
	const char *v, *t;
	char sec[40], line[256], hdr[44];
	int pos = 0, i;
	while ( ( pos = ini_next( "System.ini", "Add", pos, &v ) ) >= 0 ) {
		const char *bar;
		if ( !( t = cond( v ) ) || !( bar = strchr( t, '|' ) ) )
			continue;
		sprintf( sec, "%.*s", (int)( bar - t ), t );
		expand( line, bar + 1, sizeof(line) );
		sprintf( hdr, "[%s]", sec );
		for ( i = 0; i < ned && strnicmp( ed[i], hdr, strlen( hdr ) ); i++ );
		if ( i == ned ) {
			ed_insert( ned, "" );
			ed_insert( ned, hdr );
			i = ned - 1;
		}
		ed_insert( i + 1, line );
	}
}

static int has_active( const char *section, const char *key )
{
	const char *v;
	int pos = 0;
	while ( ( pos = ini_next( section, key, pos, &v ) ) >= 0 )
		if ( !strcmp( key, "Remove" ) || cond( v ) )
			return 1;
	return 0;
}

static void edit_file( const char *path, const char *section, int kind )
{
	if ( !has_active( section, "Add" ) && !has_active( section, "Remove" ) && !has_active( section, "Extend" ) )
		return;
	if ( ed_load( path ) && uninstall )
		return;
	if ( !uninstall && !stricmp( ini_get( section, "Replace", "no" ), "yes" ) )
		ed_free();   /* a new file (the old one is kept as backup) */
	ed_remove( section );
	if ( kind != 2 )
		ed_unextend( section );
	if ( !uninstall ) {
		if ( kind == 2 )
			ed_add_ini();
		else
			ed_add_dos( section, kind == 0 );
	}
	if ( ed_save( path ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "WriteError", "Cannot write" ), path );
		fail( msg );
	}
	ed_free();
}

/* --- setup steps ------------------------------------------------------ */

static void find_windows( void )
{
	const char *path = getenv( "PATH" );
	char p[80], dir[80];
	if ( path )
		while ( *path ) {
			int n = 0;
			while ( path[n] && path[n] != ';' )
				n++;
			sprintf( dir, "%.*s", n, path );
			path_cat( p, dir, "WIN.COM" );
			if ( n && file_exists( p ) ) {
				strcpy( windir, dir );
				return;
			}
			path += n;
			if ( *path )
				path++;
		}
	sprintf( p, "%s\\WINDOWS\\WIN.COM", boot );
	if ( file_exists( p ) )
		sprintf( windir, "%s\\WINDOWS", boot );
}

/* [Find] NAME=FILE1 FILE2 ...: %NAME% = path of the first one found in
 * %BOOT%\DOS, the PATH, %WINDIR%, %WINDIR%\COMMAND, %BOOT%\ or one of the
 * paths named in CONFIG.SYS / AUTOEXEC.BAT; condition ?NAME = found */
static int find_in_dir( const char *dir, const char *file, char *out )
{
	char p[80];
	if ( !*dir || strlen( dir ) + strlen( file ) > 76 )
		return 0;
	path_cat( p, dir, file );
	if ( !file_exists( p ) )
		return 0;
	strcpy( out, p );
	return 1;
}

static int find_in_cfg( const char *cfg, const char *file, char *out )
{
	FILE *f = fopen( cfg, "r" );
	char buf[256];
	int l = strlen( file ), found = 0;
	if ( !f )
		return 0;
	while ( !found && fgets( buf, sizeof(buf), f ) ) {
		/* words of the line (strtok is in use by find_files) */
		char *p = buf;
		while ( *p && !found ) {
			char *tok;
			int n;
			while ( *p && strchr( " =\t\r\n,;", *p ) )
				p++;
			tok = p;
			while ( *p && !strchr( " =\t\r\n,;", *p ) )
				p++;
			n = p - tok;
			if ( *p )
				*p++ = 0;
			if ( n > l && tok[n - l - 1] == '\\' && !stricmp( tok + n - l, file ) && file_exists( tok ) && n < 80 ) {
				strcpy( out, tok );
				strupr( out );
				found = 1;
			}
		}
	}
	fclose( f );
	return found;
}

static int find_file( const char *file, char *out )
{
	const char *path = getenv( "PATH" );
	char d[80];
	sprintf( d, "%s\\DOS", boot );
	if ( find_in_dir( d, file, out ) )
		return 1;
	if ( path )
		while ( *path ) {
			int n = 0;
			while ( path[n] && path[n] != ';' )
				n++;
			if ( n < 70 ) {
				sprintf( d, "%.*s", n, path );
				if ( find_in_dir( d, file, out ) )
					return 1;
			}
			path += n;
			if ( *path )
				path++;
		}
	if ( find_in_dir( windir, file, out ) )
		return 1;
	if ( *windir ) {
		path_cat( d, windir, "COMMAND" );
		if ( find_in_dir( d, file, out ) )
			return 1;
	}
	sprintf( d, "%s\\", boot );
	if ( find_in_dir( d, file, out ) )
		return 1;
	sprintf( d, "%s\\CONFIG.SYS", boot );
	if ( find_in_cfg( d, file, out ) )
		return 1;
	sprintf( d, "%s\\AUTOEXEC.BAT", boot );
	return find_in_cfg( d, file, out );
}

static void find_files( void )
{
	int pos = 0;
	const char *v;
	nfind = 0;
	while ( nfind < MAXFIND && ( pos = ini_text( "Find", pos, &v ) ) >= 0 ) {
		const char *eq = strchr( v, '=' );
		char list[120], *f;
		if ( !eq || eq - v > 15 )
			continue;
		sprintf( finds[nfind].name, "%.*s", (int)( eq - v ), v );
		finds[nfind].path[0] = finds[nfind].file[0] = 0;
		/* first on the installation disk: then it is installed with the
		 * rest (the path is set by find_dest() when %DIR% is known) */
		sprintf( list, "%.119s", eq + 1 );
		for ( f = strtok( list, " ," ); f; f = strtok( NULL, " ," ) ) {
			char p[80];
			path_cat( p, srcdir, f );
			if ( strlen( f ) < 13 && file_exists( p ) ) {
				strcpy( finds[nfind].file, f );
				strupr( finds[nfind].file );
				strcpy( finds[nfind].path, p );
				break;
			}
		}
		/* else on the computer */
		if ( !finds[nfind].file[0] ) {
			sprintf( list, "%.119s", eq + 1 );
			for ( f = strtok( list, " ," ); f; f = strtok( NULL, " ," ) ) {
				char out[80];
				if ( find_file( f, out ) ) {
					strcpy( finds[nfind].path, out );
					break;
				}
			}
		}
		nfind++;
	}
}

/* the files of [Find] that come from the installation disk go to %DIR% */
static void find_dest( void )
{
	int i;
	for ( i = 0; i < nfind; i++ )
		if ( finds[i].file[0] )
			path_cat( finds[i].path, dest, finds[i].file );
}

static void show_text( const char *title, const char *file, const char **buttons, int n, int must )
{
	char p[80], **l;
	int nl, rc;
	path_cat( p, srcdir, file );
	if ( !file_exists( p ) && *dest )   /* the disk may have been changed */
		path_cat( p, dest, file );
	if ( !( l = text_load( p, &nl ) ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "MissingFile", "File not found:" ), p );
		fail( msg );
	}
	scr_keys( T( "KeysView", "PgUp=Page up  PgDn=Page down  Enter=Select  Esc=Abort" ) );
	rc = scr_view( title, l, nl, buttons, n, 0 );
	text_free( l, nl );
	if ( must && rc != 0 )
		abort_setup();
}

static void license( void )
{
	if ( ini_get( "Setup", "License", NULL ) ) {
		static const char *b[2];
		b[0] = T( "Accept", "I accept" );
		b[1] = T( "Decline", "Decline" );
		show_text( T( "LicenseTitle", "License agreement" ), ini_get( "Setup", "License", NULL ), b, 2, 1 );
		scr_desktop();
	}
}

static void welcome( void )
{
	const char *v;
	static char text[1200];
	static const char *b[2];
	int pos = 0;
	b[0] = T( "Continue", "Continue" );
	b[1] = T( "Exit", "Exit" );
	text[0] = 0;
	/* the lines of a paragraph are joined, empty lines separate paragraphs */
	while ( ( pos = ini_text( "Welcome", pos, &v ) ) >= 0 && strlen( text ) + strlen( v ) < sizeof(text) - 3 ) {
		size_t l = strlen( text );
		if ( !*v ) {
			if ( l && text[l - 1] != '\n' )
				strcat( text, "\n\n" );
			continue;
		}
		if ( l && text[l - 1] != '\n' )
			strcat( text, " " );
		strcat( text, v );
	}
	while ( ( pos = strlen( text ) ) > 0 && text[pos - 1] == '\n' )
		text[pos - 1] = 0;
	if ( !text[0] )
		sprintf( text, "%s %s.", T( "WelcomeText", "This program installs" ), ini_get( "Setup", "Product", "the software" ) );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	if ( scr_message( T( "WelcomeTitle", "Welcome" ), text, b, 2, 0, 0 ) != 0 )
		abort_setup();
}

static void read_options( void )
{
	const char *v;
	int pos = 0;
	while ( nopt < MAXOPT && ( pos = ini_next( "Options", "Option", pos, &v ) ) >= 0 ) {
		/* Option=on|Label|value: %On% expands to value if the option is on */
		const char *bar = strchr( v, '|' ), *bar2;
		static char labels[MAXOPT][70];
		opt_on[nopt] = !strnicmp( v, "on", 2 ) || *v == '1' || toupper( *v ) == 'Y';
		bar = bar ? bar + 1 : v;
		bar2 = strchr( bar, '|' );
		sprintf( labels[nopt], "%.*s", bar2 ? (int)( bar2 - bar ) : 69, bar );
		if ( bar2 )
			sprintf( opt_value[nopt], "%.23s", bar2 + 1 );
		opt_label[nopt] = labels[nopt];
		nopt++;
	}
}

static void ask_options( void )
{
	if ( !nopt )
		return;
	scr_keys( T( "KeysOpt", "Space=On/off  Enter=Continue  Esc=Abort" ) );
	if ( scr_checklist( T( "OptionsTitle", "Options" ),
		T( "OptionsText", "Select the parts to install (Space switches on/off):" ), opt_label, opt_on, nopt ) )
		abort_setup();
}

static void ask_dirs( void )
{
	strcpy( dest, ini_get( "Setup", "DefaultDir", "C:\\PROGRAM" ) );
	expand( dest, dest, sizeof(dest) );
	scr_keys( T( "KeysInput", "Enter=Continue  Esc=Abort" ) );
	for ( ;; ) {
		if ( scr_input( T( "DirTitle", "Destination" ), T( "DirText", "Install the files to this directory:" ), dest, 64 ) )
			abort_setup();
		if ( strlen( dest ) >= 3 && dest[1] == ':' && dest[2] == '\\' && !mkdirs( dest ) )
			break;
		{
			static const char *ok[] = { "OK" };
			scr_message( T( "ErrorTitle", "Error" ), T( "DirError", "Please enter a full path (e.g. C:\\DRIVERS) that can be created." ), ok, 1, 0, 1 );
		}
	}
	find_dest();
	if ( has_active( "System.ini", "Add" ) ) {
		find_windows();
		if ( scr_input( T( "WinTitle", "Windows" ),
			T( "WinText", "Windows directory for the SYSTEM.INI entries (empty: no Windows):" ), windir, 64 ) )
			abort_setup();
	}
}

static int confirm( void )
{
	static char text[1500];
	static const char *b[2];
	const char *v, *t;
	char line[160];
	int pos;
	b[0] = T( "Install", "Install" );
	b[1] = T( "Cancel", "Cancel" );
	sprintf( text, "%s %s\n", T( "SumFiles", "Files to:" ), dest );
	pos = 0;
	while ( ( pos = ini_next( "Config.sys", "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			if ( strlen( text ) + strlen( line ) + 40 < sizeof(text) )
				sprintf( text + strlen( text ), "%s\\CONFIG.SYS: %s\n", boot, line );
		}
	pos = 0;
	while ( ( pos = ini_next( "Autoexec.bat", "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			if ( strlen( text ) + strlen( line ) + 40 < sizeof(text) )
				sprintf( text + strlen( text ), "%s\\AUTOEXEC.BAT: %s\n", boot, line );
		}
	pos = 0;   /* Extend: the text to append (or the new line) */
	while ( ( pos = ini_next( "Autoexec.bat", "Extend", pos, &v ) ) >= 0 ) {
		char m[256], x[256], n[256];
		if ( ( t = cond( v ) ) != NULL && !ext_split( t, m, x, n ) && strlen( text ) + strlen( x ) + 60 < sizeof(text) )
			sprintf( text + strlen( text ), "%s\\AUTOEXEC.BAT: %s ... %s\n", boot, m, x );
	}
	if ( windir[0] ) {
		pos = 0;
		while ( ( pos = ini_next( "System.ini", "Add", pos, &v ) ) >= 0 )
			if ( ( t = cond( v ) ) != NULL && strchr( t, '|' ) ) {
				expand( line, strchr( t, '|' ) + 1, sizeof(line) );
				sprintf( text + strlen( text ), "SYSTEM.INI [%.*s]: %s\n", (int)( strchr( t, '|' ) - t ), t, line );
			}
	}
	strcat( text, T( "SumBackup", "\nThe original files are kept as *.G01, *.G02, ..." ) );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	return scr_message( T( "SumTitle", "Ready to install" ), text, b, 2, 0, 0 ) == 0;
}

/* is %NAME% used in an active line of the configuration sections? */
static int find_used( const char *name )
{
	static const char *sect[] = { "Config.sys", "Autoexec.bat", "System.ini" };
	static const char *keys[] = { "Add", "Extend" };
	char var[20];
	const char *v, *t;
	int s, k, pos;
	sprintf( var, "%%%s%%", name );
	for ( s = 0; s < 3; s++ )
		for ( k = 0; k < 2; k++ )
			for ( pos = 0; ( pos = ini_next( sect[s], keys[k], pos, &v ) ) >= 0; )
				if ( ( t = cond( v ) ) != NULL && contains( t, var ) )
					return 1;
	return 0;
}

/* --- copying ---------------------------------------------------------- */

static const char *progtitle;
static char progname[40];

void gdi_progress( unsigned long done, unsigned long total )
{
	scr_progress( progtitle, progname, done ? done : 1, total ? total : 1 );
}

void gdi_progress_name( const char *name )
{
	const char *b = strrchr( name, '\\' );
	sprintf( progname, "%.39s", b ? b + 1 : name );
}

/* ask for disk n of the archive; 0 = inserted, 1 = cancel */
int gdi_ask_disk( int n )
{
	static const char *b[2];
	char key[4], msg[200];
	int rc;
	b[0] = T( "OK", "OK" );
	b[1] = T( "Cancel", "Cancel" );
	sprintf( key, "%d", n );
	sprintf( msg, T( "InsertDisk", "Please insert disk %d into drive %c:" ), n, srcdir[0] );
	if ( ini_get( "Disks", key, NULL ) )
		sprintf( msg + strlen( msg ), "\n\n%s", ini_get( "Disks", key, "" ) );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	rc = scr_message( T( "DiskTitle", "Next disk" ), msg, b, 2, 0, 0 );
	scr_desktop();
	scr_progress( progtitle, "", 0, 1 );
	return rc != 0;
}

static void log_file( const char *path )
{
	if ( logf )
		fprintf( logf, "%s\n", path );
}

/* [Files] line "cond|PATTERN=DEST": PATTERN is a name or "DIR\*" (all
 * archive files below DIR\, "*" = all); DEST is a name or directory,
 * relative to %DIR% unless it is a full path.  Returns 1 if name matches
 * and sets the target path. */
static int file_target( const char *line, const char *name, char *path )
{
	char pat[80], dst[80];
	const char *eq = strchr( line, '=' ), *rest;
	int l, wild;
	sprintf( pat, "%.*s", eq ? (int)( eq - line ) : 79, line );
	l = strlen( pat );
	wild = l && pat[l - 1] == '*';
	if ( wild ) {
		if ( strnicmp( name, pat, l - 1 ) )
			return 0;
		rest = name + l - 1;
	} else {
		if ( stricmp( name, pat ) )
			return 0;
		rest = name;
	}
	if ( eq ) {
		expand( dst, eq + 1, sizeof(dst) );
		if ( dst[1] != ':' ) {
			char t[80];
			path_cat( t, dest, dst );
			strcpy( dst, t );
		}
		if ( wild )
			path_cat( path, dst, rest );
		else
			strcpy( path, dst );
	} else
		path_cat( path, dest, wild ? rest : name );
	return 1;
}

static void make_parent( const char *path )
{
	char d[80], *b;
	strcpy( d, path );
	if ( ( b = strrchr( d, '\\' ) ) != NULL && b - d > 2 ) {
		*b = 0;
		mkdirs( d );
	}
}

/* target of an archive file (callback of arc_install): 0 = install */
static int arc_target( const char *name, char *path )
{
	const char *v, *t;
	int pos = 0;
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL && file_target( t, name, path ) ) {
			make_parent( path );
			log_file( path );
			return 0;
		}
	return -1;
}

static void copy_all( void )
{
	const char *v, *t, *arc = ini_get( "Setup", "Archive", NULL );
	char from[80], to[80], name[40];
	int pos = 0, total = 0, done = 0, i;
	int unin = !stricmp( ini_get( "Setup", "Uninstall", dosmode ? "no" : "yes" ), "yes" );

	progtitle = T( "CopyTitle", "Copying files" );
	if ( unin ) {
		path_cat( to, dest, "INSTALL.LOG" );
		logf = fopen( to, "w" );
	}
	/* [Dirs]: directories to create (cond|path) */
	while ( ( pos = ini_lines( "Dirs", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( to, t, sizeof(to) );
			mkdirs( to );
		}
	pos = 0;
	/* plain files next to INSTALL.EXE */
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 )
		if ( cond( v ) )
			total++;
	if ( unin )
		total += 2;
	pos = 0;
	scr_progress( progtitle, "", 0, total );
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
		if ( !( t = cond( v ) ) )
			continue;
		sprintf( name, "%.*s", strchr( t, '=' ) ? (int)( strchr( t, '=' ) - t ) : 39, t );
		if ( strchr( name, '*' ) )
			continue;
		path_cat( from, srcdir, name );
		if ( arc && !file_exists( from ) )
			continue;   /* in the archive */
		file_target( t, name, to );
		make_parent( to );
		scr_progress( progtitle, name, ++done, total );
		if ( copy_file( from, to ) ) {
			char msg[160];
			sprintf( msg, "%s %s -> %s", T( "CopyError", "Cannot copy" ), from, to );
			fail( msg );
		}
		log_file( to );
	}
	/* [Find] files from the installation disk that are used */
	for ( i = 0; i < nfind; i++ )
		if ( finds[i].file[0] && find_used( finds[i].name ) ) {
			path_cat( from, srcdir, finds[i].file );
			scr_progress( progtitle, finds[i].file, done, total );
			if ( copy_file( from, finds[i].path ) ) {
				char msg[160];
				sprintf( msg, "%s %s -> %s", T( "CopyError", "Cannot copy" ), from, finds[i].path );
				fail( msg );
			}
			log_file( finds[i].path );
		}
	if ( unin ) {
		/* INSTALL /U in the target directory removes the installation */
		path_cat( from, srcdir, "INSTALL.EXE" );
		path_cat( to, dest, "INSTALL.EXE" );
		scr_progress( progtitle, "INSTALL.EXE", ++done, total );
		copy_file( from, to );
		path_cat( from, srcdir, "INSTALL.INI" );
		path_cat( to, dest, "INSTALL.INI" );
		scr_progress( progtitle, "INSTALL.INI", ++done, total );
		copy_file( from, to );
	}
	if ( arc ) {
		char base[80];
		const char *err;
		path_cat( base, srcdir, arc );
		scr_desktop();
		scr_progress( progtitle, "", 0, 1 );
		if ( ( err = arc_install( base, arc_target ) ) != NULL ) {
			char msg[160];
			if ( !strcmp( err, "Cancelled" ) )
				abort_setup();
			if ( !strcmp( err, "CreateError" ) )
				sprintf( msg, "%s %s", T( "CopyError", "Cannot copy" ), arc_errfile() );
			else if ( !strcmp( err, "CrcError" ) || !strcmp( err, "BadArchive" ) || !strcmp( err, "ReadError" ) )
				sprintf( msg, "%s (%s)", T( "DiskError", "The disk is damaged or not readable." ), progname );
			else
				sprintf( msg, "%s", T( err, err ) );
			fail( msg );
		}
	}
	if ( logf ) {
		fclose( logf );
		logf = NULL;
	}
}

/* [Run] Before= / After=: programs to run (cond|command line); a program
 * without a path is taken from the directory of INSTALL.EXE */
static int run_program( const char *cmdline )
{
	char line[200], prog[80], *argv[16];
	int argc = 0, rc;
	char *p;
	expand( line, cmdline, sizeof(line) );
	for ( p = strtok( line, " " ); p && argc < 15; p = strtok( NULL, " " ) )
		argv[argc++] = p;
	argv[argc] = NULL;
	if ( !argc )
		return -1;
	if ( !strchr( argv[0], '\\' ) && !strchr( argv[0], ':' ) )
		path_cat( prog, srcdir, argv[0] );
	else
		strcpy( prog, argv[0] );
	scr_exit();
	rc = spawnv( P_WAIT, prog, (const char * const *)argv );
	scr_init();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne DOS Installer" ), "" );
	scr_desktop();
	return rc;
}

static void run_all( const char *key )
{
	const char *v, *t;
	int pos = 0;
	while ( ( pos = ini_next( "Run", key, pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL && run_program( t ) == -1 ) {
			char msg[160];
			sprintf( msg, "%s %s", T( "RunError", "Cannot run" ), t );
			fail( msg );
		}
}

static void edit_all( void )
{
	char p[80];
	sprintf( p, "%s\\CONFIG.SYS", boot );
	edit_file( p, "Config.sys", 0 );
	sprintf( p, "%s\\AUTOEXEC.BAT", boot );
	edit_file( p, "Autoexec.bat", 1 );
	if ( windir[0] ) {
		path_cat( p, windir, "SYSTEM.INI" );
		if ( file_exists( p ) )
			edit_file( p, "System.ini", 2 );
	}
}

static void remove_all( void )
{
	const char *v, *t;
	char p[80], name[40];
	int pos = 0;
	static const char *b[2];
	b[0] = T( "Remove", "Remove" );
	b[1] = T( "Cancel", "Cancel" );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	if ( scr_message( T( "UninstTitle", "Remove" ), T( "UninstText", "Remove the installation from this directory and its entries from CONFIG.SYS, AUTOEXEC.BAT and SYSTEM.INI?" ), b, 2, 1, 0 ) != 0 )
		abort_setup();
	strcpy( dest, srcdir );
	for ( pos = 0; pos < MAXOPT; pos++ )
		opt_on[pos] = 1;
	find_windows();
	edit_all();
	path_cat( p, dest, "INSTALL.LOG" );
	if ( ( logf = fopen( p, "r" ) ) != NULL ) {
		/* the files installed (from archives: also below %DIR%) */
		char l[90];
		while ( fgets( l, sizeof(l), logf ) ) {
			char *e = strchr( l, '\n' ), *b;
			if ( e )
				*e = 0;
			remove( l );
			while ( ( b = strrchr( l, '\\' ) ) != NULL && b - l > 2 && stricmp( l, dest ) ) {
				*b = 0;
				if ( rmdir( l ) )
					break;
			}
		}
		fclose( logf );
		logf = NULL;
		remove( p );
	}
	pos = 0;
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
		const char *eq;
		t = strchr( v, '|' ) ? strchr( v, '|' ) + 1 : v;
		if ( strchr( t, '*' ) )
			continue;
		eq = strchr( t, '=' );
		strcpy( name, eq ? eq + 1 : t );
		if ( name[1] == ':' )
			strcpy( p, name );
		else
			path_cat( p, dest, name );
		remove( p );
	}
	path_cat( p, dest, "INSTALL.INI" );
	remove( p );
	path_cat( p, dest, "INSTALL.EXE" );
	remove( p );
	rmdir( dest );
}

/* --- DOS mode ----------------------------------------------------------- */

static int __far harderr_fail( unsigned deverr, unsigned errcode, unsigned __far *devhdr )
{
	(void)deverr; (void)errcode; (void)devhdr;
	return _HARDERR_FAIL;
}

/* 0 = formatted, 1 = drive exists but is not formatted, 2 = no drive */
static int drive_state( char d )
{
	union REGS r;
	int n = toupper( d ) - 'A' + 1;
	r.x.ax = 0x4408;   /* removable media? fails for a missing drive */
	r.h.bl = n;
	intdos( &r, &r );
	if ( r.x.cflag && r.x.ax == 0x0F )
		return 2;
	r.h.ah = 0x36;     /* free space: fails for an unformatted drive */
	r.h.dl = n;
	intdos( &r, &r );
	return r.x.ax == 0xFFFF ? 1 : 0;
}

static void reboot( void )
{
	union REGS r;
	void ( __far *reset )( void ) = (void ( __far * )( void ))MK_FP( 0xFFFF, 0 );
	r.h.ah = 0x0D;     /* flush the DOS buffers */
	intdos( &r, &r );
	*(unsigned short __far *)MK_FP( 0x40, 0x72 ) = 0x1234;   /* warm boot */
	reset();
}

static void ask_reboot( const char *text )
{
	static const char *b[1];
	b[0] = T( "Restart", "Restart" );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	scr_message( T( "RestartTitle", "Restart" ), text, b, 1, 0, 0 );
	scr_exit();
	reboot();
}

/* the hard disk: partition (FDISK, then restart) and format it */
static void dos_prepare( void )
{
	static const char *b[2];
	char msg[300];
	int st;
	b[1] = T( "Exit", "Exit" );
	_harderr( harderr_fail );
	while ( ( st = drive_state( boot[0] ) ) != 0 ) {
		scr_desktop();
		scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
		if ( st == 2 ) {
			b[0] = T( "StartFdisk", "Start FDISK" );
			sprintf( msg, T( "FdiskText", "There is no drive %s yet. FDISK starts now: create a primary DOS partition (and make it active), then leave FDISK. The computer restarts and Setup continues." ), boot );
			if ( scr_message( T( "DiskSetupTitle", "Hard disk" ), msg, b, 2, 0, 0 ) )
				abort_setup();
			run_program( ini_get( "Setup", "Fdisk", "FDISK.EXE" ) );
			if ( drive_state( boot[0] ) == 2 ) {
				sprintf( msg, T( "RestartText", "The computer must restart now. Leave the setup disk in drive %c:, Setup continues afterwards." ), srcdir[0] );
				ask_reboot( msg );
			}
		} else {
			char cmd[100];
			b[0] = T( "StartFormat", "Format" );
			sprintf( msg, T( "FormatText", "Drive %s is not formatted yet. Format it now? All data on it is lost." ), boot );
			if ( scr_message( T( "DiskSetupTitle", "Hard disk" ), msg, b, 2, 0, 0 ) )
				abort_setup();
			sprintf( cmd, "%s", ini_get( "Setup", "Format", "FORMAT.COM %BOOT%" ) );
			run_program( cmd );
		}
	}
}

static void read_options( void );
static void welcome( void );
static void ask_dirs( void );
static void ask_options( void );
static int confirm( void );
static void license( void );

/* the setup steps of an INI (not DOS mode) */
static void install_steps( void )
{
	welcome();
	license();
	scr_desktop();
	ask_dirs();
	scr_desktop();
	ask_options();
	scr_desktop();
	if ( !confirm() )
		abort_setup();
	scr_desktop();
	run_all( "Before" );
	copy_all();
	edit_all();
	run_all( "After" );
}

/* the INI (already loaded) of srcdir dir becomes the current one */
static void use_ini( const char *dir )
{
	strcpy( srcdir, dir );
	nopt = 0;
	memset( opt_on, 0, sizeof(opt_on) );
	memset( opt_value, 0, sizeof(opt_value) );
	dest[0] = windir[0] = 0;
	dosmode = !stricmp( ini_get( "Setup", "Mode", "" ), "DOS" );
	if ( !dosmode )
		find_windows();
	find_files();
	read_options();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne DOS Installer" ), "" );
	scr_desktop();
}

/* further installations: the INSTALL.INI of another disk or directory is
 * installed by this program for the new system (%BOOT%), then the DOS
 * setup continues */
static void extras( void )
{
	static const char *b[2];
	static char dir[80], ini[80], maindir[80], maindest[80];
	strcpy( maindir, srcdir );
	strcpy( maindest, dest );
	strcpy( dir, ini_get( "Setup", "ExtrasDir", "A:\\" ) );
	for ( ;; ) {
		b[0] = T( "InstallMore", "Install" );
		b[1] = T( "Finish", "Finish" );
		scr_desktop();
		scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
		if ( scr_message( T( "ExtrasTitle", "Additional software" ),
			T( "ExtrasText", "Do you want to install additional software now (drivers, tools)? Insert its disk or enter the directory that contains its INSTALL.INI." ),
			b, 2, 0, 0 ) )
			break;
		scr_desktop();
		scr_keys( T( "KeysInput", "Enter=Continue  Esc=Abort" ) );
		if ( scr_input( T( "ExtrasTitle", "Additional software" ), T( "ExtrasDirText", "Directory with INSTALL.INI:" ), dir, 64 ) )
			continue;
		path_cat( ini, dir, "INSTALL.INI" );
		if ( !file_exists( ini ) ) {
			static const char *ok[] = { "OK" };
			char msg[120];
			sprintf( msg, "%s %s", T( "MissingFile", "File not found:" ), ini );
			scr_message( T( "ErrorTitle", "Error" ), msg, ok, 1, 0, 1 );
			continue;
		}
		ini_save();
		if ( ini_load( ini ) ) {
			ini_restore();
			continue;
		}
		use_ini( dir );
		in_extra = 1;
		if ( !setjmp( extra_abort ) ) {
			install_steps();
			scr_desktop();
			{
				static const char *ok[1];
				ok[0] = T( "OK", "OK" );
				scr_message( T( "DoneTitle", "Setup complete" ),
					ini_get( "Setup", "Done", T( "DoneText", "The installation is complete." ) ), ok, 1, 0, 0 );
			}
		}
		in_extra = 0;
		/* back to the DOS setup */
		ini_restore();
		use_ini( maindir );
		strcpy( dest, maindest );
	}
}

static void finish( void )
{
	static const char *b[2];
	const char *readme = ini_get( "Setup", "Readme", NULL );
	int rc;
	b[0] = T( "OK", "OK" );
	b[1] = T( "ShowReadme", "Readme" );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	rc = scr_message( T( "DoneTitle", "Setup complete" ),
		uninstall ? T( "UninstDone", "The installation has been removed. Restart the computer to unload the drivers." )
		          : ini_get( "Setup", "Done", T( "DoneText", "The installation is complete. Restart the computer to load the drivers." ) ),
		b, readme && !uninstall ? 2 : 1, 0, 0 );
	if ( rc == 1 ) {
		static const char *ok[1];
		ok[0] = T( "OK", "OK" );
		show_text( T( "ReadmeTitle", "Readme" ), readme, ok, 1, 0 );
	}
	if ( dosmode ) {
		char msg[200];
		scr_desktop();
		sprintf( msg, T( "DosDoneText", "Remove the disk from drive %c: and restart the computer." ), srcdir[0] );
		ask_reboot( msg );
	}
}

static void usage( void )
{
	printf( "GeosOne DOS Installer " GDI_VERSION " - GNU GPL version 3\n"
	        "INSTALL [/U] [/B:d:] [file.INI]\n"
	        "  installs as described by INSTALL.INI next to INSTALL.EXE\n"
	        "  /U    removes the installation (run INSTALL.EXE in the target directory)\n"
	        "  /B:d: drive of the system to change (CONFIG.SYS, AUTOEXEC.BAT),\n"
	        "        default: the boot drive\n" );
}

int main( int argc, char **argv )
{
	char ini[80], *s;
	union REGS r;
	int i;

	gdi_self = argv[0];
	strcpy( srcdir, argv[0] );
	if ( ( s = strrchr( srcdir, '\\' ) ) != NULL )
		*s = 0;
	else
		getcwd( srcdir, sizeof(srcdir) );
	path_cat( ini, srcdir, "INSTALL.INI" );
	for ( i = 1; i < argc; i++ ) {
		if ( argv[i][0] == '/' || argv[i][0] == '-' ) {
			if ( toupper( argv[i][1] ) == 'U' )
				uninstall = 1;
			else if ( toupper( argv[i][1] ) == 'B' && argv[i][2] == ':' && isalpha( argv[i][3] ) ) {
				boot[0] = toupper( argv[i][3] );
				bootset = 1;
			} else {
				usage();
				return 0;
			}
		} else {
			/* another INI: its files are in its directory */
			strcpy( ini, argv[i] );
			strcpy( srcdir, argv[i] );
			if ( ( s = strrchr( srcdir, '\\' ) ) != NULL )
				*s = 0;
			else
				getcwd( srcdir, sizeof(srcdir) );
			if ( strlen( srcdir ) == 2 && srcdir[1] == ':' )
				strcat( srcdir, "\\" );
		}
	}
	if ( ini_load( ini ) ) {
		printf( "INSTALL: %s not found\n", ini );
		return 2;
	}
	dosmode = !stricmp( ini_get( "Setup", "Mode", "" ), "DOS" );
	if ( dosmode && !bootset ) {
		const char *d = ini_get( "Setup", "Drive", "C:" );
		boot[0] = toupper( d[0] );
	} else if ( !bootset ) {
		r.x.ax = 0x3305;   /* boot drive */
		intdos( &r, &r );
		if ( r.h.dl >= 1 && r.h.dl <= 26 )
			boot[0] = 'A' + r.h.dl - 1;
	}
	if ( !dosmode )
		find_windows();
	find_files();

	scr_init();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne DOS Installer" ), "" );
	scr_desktop();
	read_options();
	if ( uninstall )
		remove_all();
	else {
		if ( dosmode ) {
			welcome();
			license();
			dos_prepare();
			scr_desktop();
			ask_dirs();
			scr_desktop();
			ask_options();
			scr_desktop();
			if ( !confirm() )
				abort_setup();
			scr_desktop();
			run_all( "Before" );
			copy_all();
			edit_all();
			run_all( "After" );
		} else
			install_steps();
		if ( !stricmp( ini_get( "Setup", "Extras", "no" ), "yes" ) )
			extras();
	}
	scr_desktop();
	finish();
	scr_exit();
	return 0;
}
