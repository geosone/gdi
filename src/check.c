/* check.c - GeosOne Dual Installer: INSTALL /CHECK and build/gdicheck, a
 * check of an INSTALL.INI for the mistakes Setup would only find at the
 * user's: unknown sections and keys, lines too long, files missing, unknown
 * names and option numbers in conditions.  Plain C, also for the host.
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef __WATCOMC__
#include <dirent.h>
#endif
#include "check.h"

#define MAXNAMES  64
#define LINESIZE  600   /* ini.c reads lines of 511 characters */
#define INILINES  400   /* ini.c MAXLINES */

static char (*names)[24];   /* of [Find] and [Detect], MAXNAMES */
static int nnames, nopts, problems;
static void (*report)( const char *text );
static char *dir;   /* of the INI, 160 */
static int lineno;

static int same( const char *a, const char *b )
{
	while ( *a && tolower( (unsigned char)*a ) == tolower( (unsigned char)*b ) )
		a++, b++;
	return tolower( (unsigned char)*a ) == tolower( (unsigned char)*b );
}

static int same_n( const char *a, const char *b, int n )
{
	while ( n > 0 && *a && tolower( (unsigned char)*a ) == tolower( (unsigned char)*b ) )
		a++, b++, n--;
	return !n || tolower( (unsigned char)*a ) == tolower( (unsigned char)*b );
}

static void problem( const char *fmt, const char *a, const char *b )
{
	char text[200];
	char line[16];
	sprintf( line, "%d", lineno );
	sprintf( text, "line %s: ", line );
	sprintf( text + strlen( text ), fmt, a, b );
	report( text );
	problems++;
}

static char *trim( char *s )
{
	char *e;
	while ( *s == ' ' || *s == '\t' )
		s++;
	e = s + strlen( s );
	while ( e > s && ( e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t' ) )
		*--e = 0;
	return s;
}

/* the known sections; the keys of those whose lines are key=value */
static const char *sections[] = {
	"Setup", "Welcome", "Options", "Find", "Detect", "Files", "Dirs", "Run",
	"Config.sys", "Autoexec.bat", "System.ini", "Win.ini", "Ini", "ProgMan",
	"HexPatch", "Registry", "Keep", "Newer", "Installer", "Disks", "Languages",
	"Text", "Extras", NULL
};
static const char *setup_keys[] = {
	"Title", "Product", "Version", "Id", "License", "Readme", "DefaultDir",
	"Done", "Uninstall", "Requires", "Conflicts", "Texts", "Mode", "Drive",
	"Archive", "Extras", "ExtrasDir", "Fdisk", "Format", "Windows", "Space",
	"MinDos", "MinWindows", "SetupDir", "Need", NULL
};
static const char *options_keys[] = { "Option", NULL };
static const char *run_keys[] = { "Before", "After", "Windows", "Uninstall", NULL };
static const char *dos_keys[] = { "Remove", "Add", "Position", "Extend", "Replace", "PathAppend", "PathRemove", "Program", NULL };
static const char *system_keys[] = { "Remove", "Add", "Set", "Change", "Replace", NULL };
static const char *set_keys[] = { "Set", NULL };
static const char *progman_keys[] = { "Item", NULL };
static const char *hexpatch_keys[] = { "Patch", "PatchIfPresent", "PatchVariant", NULL };
static const char *languages_keys[] = { "Choice", "Config", "Default", NULL };
static const char *extras_keys[] = { "Extra", NULL };
static const char **section_keys[] = {
	setup_keys, NULL, options_keys, NULL, NULL, NULL, NULL, run_keys,
	dos_keys, dos_keys, system_keys, set_keys, set_keys, progman_keys,
	hexpatch_keys, NULL, NULL, NULL, NULL, NULL, languages_keys,
	NULL, extras_keys
};

static int known( const char **list, const char *key )
{
	for ( ; *list; list++ )
		if ( same( *list, key ) )
			return 1;
	return 0;
}

/* name, or name\sub\file, below dir: case does not matter */
static int file_there( const char *name )
{
	char path[200];
#ifdef __WATCOMC__
	FILE *f;
	sprintf( path, "%s\\%s", dir, name );
	if ( ( f = fopen( path, "rb" ) ) == NULL )
		return 0;
	fclose( f );
	return 1;
#else
	char part[100];
	const char *p = name;
	strcpy( path, dir );
	while ( *p ) {
		DIR *d;
		struct dirent *e;
		int n = 0, found = 0;
		while ( p[n] && p[n] != '\\' && p[n] != '/' )
			n++;
		sprintf( part, "%.*s", n > 99 ? 99 : n, p );
		p += n;
		if ( *p )
			p++;
		if ( !n )
			continue;
		if ( ( d = opendir( path ) ) == NULL )
			return 0;
		while ( ( e = readdir( d ) ) != NULL )
			if ( same( e->d_name, part ) && strlen( path ) + strlen( e->d_name ) + 2 < sizeof(path) ) {
				strcat( path, "/" );
				strcat( path, e->d_name );
				found = 1;
				break;
			}
		closedir( d );
		if ( !found )
			return 0;
	}
	return 1;
#endif
}

/* "1,!2,?CDEX|text" like cond() of core.c; returns the text after the
 * condition (or v), and reports unknown names and option numbers */
static const char *condition( const char *v )
{
	const char *bar = strchr( v, '|' ), *p = v;
	char name[24];
	int i, n;
	if ( !bar )
		return v;
	/* plain text with '|' (386Enh|...) as for cond(): an entry is not a
	 * number or ?NAME */
	while ( p < bar ) {
		while ( *p == ' ' || *p == ',' )
			p++;
		if ( p >= bar )
			break;
		if ( *p == '!' )
			p++;
		if ( *p == '?' || *p == '@' ) {
			while ( p < bar && *p != ',' && *p != ' ' )
				p++;
			continue;
		}
		if ( !isdigit( (unsigned char)*p ) )
			return v;
		while ( p < bar && isdigit( (unsigned char)*p ) )
			p++;
	}
	p = v;
	while ( p < bar ) {
		while ( *p == ' ' || *p == ',' )
			p++;
		if ( p >= bar )
			break;
		if ( *p == '!' )
			p++;
		if ( *p == '@' ) {   /* @de: a code of [Languages] Choice= */
			const char *q = p++;
			while ( p < bar && *p != ',' && *p != ' ' )
				p++;
			n = p - q;
			for ( i = 0; i < nnames && !( (int)strlen( names[i] ) == n && same_n( names[i], q, n ) ); i++ )
				;
			if ( i == nnames ) {
				sprintf( name, "%.*s", n > 23 ? 23 : n, q );
				problem( "condition %s: not a [Languages] Choice=%s", name, "" );
			}
			continue;
		}
		if ( *p == '?' ) {
			const char *q = ++p;
			while ( p < bar && *p != ',' && *p != ' ' )
				p++;
			n = p - q;
			if ( memchr( q, '\\', n ) )
				continue;   /* ?%WINDIR%\NET.EXE */
			for ( i = 0; i < nnames && !( (int)strlen( names[i] ) == n && same_n( names[i], q, n ) ); i++ )
				;
			if ( i == nnames ) {
				sprintf( name, "%.*s", n > 23 ? 23 : n, q );
				problem( "condition ?%s: not in [Find] or [Detect]%s", name, "" );
			}
			continue;
		}
		n = atoi( p );
		while ( p < bar && isdigit( (unsigned char)*p ) )
			p++;
		if ( n < 1 || n > nopts ) {
			sprintf( name, "%d", n );
			problem( "condition: there is no option %s%s", name, "" );
		}
	}
	return bar + 1;
}

/* a file named in the INI must be next to it */
static void need_file( const char *name, const char *what )
{
	char file[100];
	int n = 0;
	while ( *name == ' ' )
		name++;
	while ( name[n] && name[n] != ' ' && name[n] != '\t' && n < 99 )
		n++;
	sprintf( file, "%.*s", n, name );
	if ( !*file || strchr( file, '%' ) || strchr( file, '*' ) || strchr( file, '?' ) ||
		( file[0] && file[1] == ':' ) )
		return;
	if ( !file_there( file ) )
		problem( "%s %s is not next to the INI", what, file );
}

int gdi_check( const char *ini, void (*out)( const char *text ) )
{
	char *buf;
	FILE *f;
	char section[40], *s, *e, *slash;
	int pass, cur, kept, archive = 0, disks = 0, long_line, any;
	report = out;
	problems = nnames = nopts = 0;
	/* not in the data segment of INSTALL.EXE: only /CHECK needs them */
	names = malloc( MAXNAMES * sizeof(*names) );
	dir = malloc( 160 );
	buf = malloc( LINESIZE );
	if ( !names || !dir || !buf ) {
		out( "not enough memory" );
		free( names ), free( dir ), free( buf );
		return 1;
	}
	sprintf( dir, "%.159s", ini );
	slash = strrchr( dir, '\\' );
	if ( !slash || ( strrchr( dir, '/' ) && strrchr( dir, '/' ) > slash ) )
		slash = strrchr( dir, '/' );
	if ( slash )
		*slash = 0;
	else
		strcpy( dir, "." );
	for ( pass = 0; pass < 2; pass++ ) {
		if ( ( f = fopen( ini, "r" ) ) == NULL ) {
			sprintf( buf, "%.200s: not found", ini );
			out( buf );
			free( names ), free( dir ), free( buf );
			return 1;
		}
		cur = -1;
		kept = lineno = long_line = any = 0;
		*section = 0;
		while ( fgets( buf, LINESIZE, f ) ) {
			int whole = strchr( buf, '\n' ) != NULL || feof( f );
			if ( long_line ) {   /* the rest of a long line */
				long_line = !whole;
				continue;
			}
			lineno++;
			if ( !whole || strlen( buf ) > 511 ) {
				if ( pass )
					problem( "longer than 510 characters: Setup splits it%s%s", "", "" );
				long_line = !whole;
			}
			s = trim( buf );
			if ( *s == ';' || ( !*s && !any ) )
				continue;
			if ( ++kept == INILINES + 1 && pass )
				problem( "more than %s lines without comments: Setup ignores the rest%s", "400", "" );
			if ( *s == '[' ) {
				any = 1;
				if ( ( e = strchr( s, ']' ) ) == NULL ) {
					if ( pass )
						problem( "no ] in the section name %s%s", s, "" );
					continue;
				}
				*e = 0;
				sprintf( section, "%.39s", s + 1 );
				for ( cur = 0; sections[cur] && !same( sections[cur], section ); cur++ )
					;
				if ( !sections[cur] ) {
					if ( pass )
						problem( "unknown section [%s]%s", section, "" );
					cur = -1;
				}
				continue;
			}
			if ( cur < 0 || !*s )
				continue;
			e = strchr( s, '=' );
			if ( !pass ) {   /* the names, the options, archives */
				if ( ( same( section, "Find" ) || same( section, "Detect" ) ) && e &&
					nnames < MAXNAMES ) {
					sprintf( names[nnames], "%.*s", (int)( e - s > 23 ? 23 : e - s ), s );
					trim( names[nnames++] );
				} else if ( same( section, "Languages" ) && e && same_n( s, "Choice", 6 ) &&
					s[6] == '=' && strchr( e, '|' ) && nnames < MAXNAMES ) {
					sprintf( names[nnames], "@%.*s", (int)( strchr( e, '|' ) - e - 1 > 22 ? 22 : strchr( e, '|' ) - e - 1 ), e + 1 );
					trim( names[nnames++] );
				} else if ( same( section, "Options" ) && e && same_n( s, "Option", 6 ) && s[6] == '=' )
					nopts++;
				else if ( same( section, "Setup" ) && e && same_n( s, "Archive", 7 ) && s[7] == '=' )
					archive = 1;
				else if ( same( section, "Disks" ) )
					disks = 1;
				continue;
			}
			if ( section_keys[cur] ) {
				char key[40];
				if ( !e ) {
					problem( "no key= in [%s]: %s", section, s );
					continue;
				}
				sprintf( key, "%.*s", (int)( e - s > 39 ? 39 : e - s ), s );
				trim( key );
				if ( !known( section_keys[cur], key ) &&
					!( same( section, "Setup" ) && same_n( key, "License", 7 ) &&
					   isdigit( (unsigned char)key[7] ) && !key[8] ) ) {
					problem( "unknown key %s= in [%s]", key, section );
					continue;
				}
				e++;
				if ( same( section, "Setup" ) ) {
					if ( same( key, "Readme" ) )
						need_file( e, "Readme" );
					else if ( same_n( key, "License", 7 ) && key[7] && strchr( e, '|' ) )
						need_file( strchr( e, '|' ) + 1, "License" );
					else if ( same( key, "License" ) && !isdigit( (unsigned char)*e ) )
						need_file( strchr( e, '|' ) ? strchr( e, '|' ) + 1 : e, "License" );
					else if ( same( key, "Need" ) ) {
						if ( !strchr( e, '|' ) || condition( e ) == e )
							problem( "Need=%s: no condition|message%s", e, "" );
					}
					continue;
				}
				if ( same( section, "Options" ) ) {
					const char *bar = strchr( e, '|' );
					if ( !bar )
						problem( "Option=%s: no on|, off| or ?NAME|%s", e, "" );
					else if ( *e == '?' || ( *e == '!' && e[1] == '?' ) ) {
						char c[40];
						sprintf( c, "%.*s|x", (int)( bar - e > 30 ? 30 : bar - e ), e );
						condition( c );
					} else if ( !same_n( e, "on|", 3 ) && !same_n( e, "off|", 4 ) )
						problem( "Option=%s: no on|, off| or ?NAME|%s", e, "" );
					continue;
				}
				if ( same( section, "Languages" ) || same( section, "Extras" ) )
					continue;
				condition( e );
				continue;
			}
			if ( same( section, "Welcome" ) || same( section, "Text" ) ||
				same( section, "Find" ) || same( section, "Disks" ) )
				continue;
			if ( same( section, "Detect" ) ) {
				if ( e )
					need_file( e + 1, "[Detect] program" );
				continue;
			}
			if ( same( section, "Installer" ) ) {
				char path[120];
				int n;
				if ( !e )
					continue;
				*e++ = 0;
				trim( s );
				if ( same( s, "Select" ) )
					continue;
				if ( !same_n( s, "Inst", 4 ) || !isdigit( (unsigned char)s[4] ) ) {
					problem( "unknown key %s= in [%s]", s, section );
					continue;
				}
				if ( same_n( e, "on|", 3 ) )
					e += 3;
				else if ( same_n( e, "off|", 4 ) )
					e += 4;
				sprintf( path, "%.100s", e );
				n = strlen( path );
				if ( n <= 4 || !same( path + n - 4, ".INI" ) )
					strcat( path, n && path[n - 1] == '\\' ? "INSTALL.INI" : "\\INSTALL.INI" );
				need_file( path, "[Installer]" );
				continue;
			}
			s = (char *)condition( s );
			if ( same( section, "Files" ) && !archive && !disks ) {
				if ( ( e = strchr( s, '=' ) ) != NULL )
					*e = 0;
				need_file( s, "[Files]" );
			} else if ( same( section, "Registry" ) && same_n( s, "Import=", 7 ) )
				need_file( condition( s + 7 ), "[Registry] Import" );
		}
		fclose( f );
	}
	free( names ), free( dir ), free( buf );
	return problems;
}

#ifdef GDI_CHECK_MAIN
static void print( const char *text )
{
	puts( text );
}

int main( int argc, char **argv )
{
	int i, bad = 0;
	if ( argc < 2 ) {
		puts( "gdicheck INSTALL.INI ...: checks the package files of the GeosOne Dual Installer" );
		return 2;
	}
	for ( i = 1; i < argc; i++ ) {
		int n;
		printf( "%s\n", argv[i] );
		n = gdi_check( argv[i], print );
		printf( n ? "%d problems\n" : "no problems\n", n );
		bad |= n != 0;
	}
	return bad;
}
#endif
