/* install.c - GeosOne Dual Installer (INSTALL.EXE): installs a DOS program
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
#include <malloc.h>
#include "gdi.h"
#include "hexpatch.h"
#include "regtxn.h"
#include "progman.h"

#define MAXOPT   16
#define MAXLINE  600
#define MAXFIND  8
#define MAXDETECT 16

static char srcdir[80], dest[80], windir[80], boot[3] = "C:";
static char opt_on[MAXOPT];
static const char *opt_label[MAXOPT];
static char opt_value[MAXOPT][24];   /* %On%: value when option n is on */
static int nopt;
static int uninstall, keep_uninstaller, dosmode, bootset;
static const char *option_override;     /* /O:0101 from the Win16 frontend */
static struct {
	char name[16];
	char path[80];
	char file[13];   /* found on the installation disk: copied to %DIR% */
} finds[MAXFIND];   /* [Find] */
static int nfind;
static struct {
	char name[16];
	char result;
} detects[MAXDETECT];   /* [Detect]: external program returned zero */
static int ndetect;
static FILE *logf;
static int update_mode;          /* an earlier installation is updated */
static char old_version[24];
static const char *gdi_self;           /* argv[0] */
static char completion_file[80];       /* /W:path: Win16 completion status */
static char registry_plan_file[80];    /* /R:path: expanded native plan */
static int registry_family;            /* /F:31 or /F:9X */
static char task_plan_file[80];        /* /T:path: WIN.INI and groups for the frontend */
static int registry_deferred;          /* REGISTRY.PLN for the runner (GDIPxxxx.EXE) */
static int completion_status = 2;
static int completion_registered;
static jmp_buf extra_abort;            /* an extra installation was cancelled */
static int in_batch;                   /* a package of [Installer] */
static int in_extra;                   /* INSTALL.LOG: installed files */
static char selected_language[9] = "en";

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

static void write_completion( void )
{
	FILE *f;
	if ( !*completion_file )
		return;
	if ( ( f = fopen( completion_file, "w" ) ) != NULL ) {
		fprintf( f, "%d\n", completion_status );
		fclose( f );
	}
}

/* --- helpers -------------------------------------------------------- */

/* %DIR%, %WINDIR%, %BOOT%, %SRC% */
static int expand( char *out, const char *in, int max )
{
	static char copy[300];
	int o = 0;
	if ( in == out ) {   /* in place: %WINDIR% is longer than its name */
		sprintf( copy, "%.299s", in );
		in = copy;
	}
	while ( *in && o < max - 1 ) {
		const char *rep = NULL;
		int l = 0;
		if ( !strnicmp( in, "%DIR%", 5 ) ) { rep = dest; l = 5; }
		else if ( !strnicmp( in, "%WINDIR%", 8 ) ) { rep = windir; l = 8; }
		else if ( !strnicmp( in, "%BOOT%", 6 ) ) { rep = boot; l = 6; }
		else if ( !strnicmp( in, "%SRC%", 5 ) ) { rep = srcdir; l = 5; }
		else if ( !strnicmp( in, "%LANGUAGE%", 10 ) ) { rep = selected_language; l = 10; }
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
	return *in != 0;
}

static const char *find_path( const char *name, int n )
{
	int i;
	for ( i = 0; i < nfind; i++ )
		if ( (int)strlen( finds[i].name ) == n && !strnicmp( finds[i].name, name, n ) )
			return finds[i].path;
	return "";
}

/* A named condition may come from [Detect] or [Find].  [Detect] takes
 * precedence when the same name occurs in both sections. */
static int named_result( const char *name, int n )
{
	int i;
	if ( memchr( name, '\\', n ) && n < 80 ) {   /* ?%WINDIR%\NET.EXE: the file exists */
		char raw[80], path[128];
		sprintf( raw, "%.*s", n, name );
		expand( path, raw, sizeof(path) );
		return file_exists( path );
	}
	for ( i = 0; i < ndetect; i++ )
		if ( (int)strlen( detects[i].name ) == n && !strnicmp( detects[i].name, name, n ) )
			return detects[i].result != 0;
	return *find_path( name, n ) != 0;
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
			if ( named_result( q, p - q ) == neg )
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

static int condition_true( const char *condition )
{
	char value[GDI_REG_CONDITION_MAX + 3];
	const char *result;
	if ( !*condition )
		return 1;
	sprintf( value, "%s|x", condition );
	result = cond( value );
	return result != NULL && !strcmp( result, "x" );
}

/* the lines of [Registry]; Import=condition|FILE.REG (REGEDIT 3.1 format,
 * next to INSTALL.EXE) gives one Set= per "HKEY_CLASSES_ROOT\key = value"
 * line of the file, without keeping it in memory */
static FILE *reg_import;
static char reg_import_cond[40];

static int registry_next( int pos, const char **line )
{
	static char made[GDI_REG_KEY_MAX + GDI_REG_TEXT_MAX + 64];
	char buf[GDI_REG_KEY_MAX + GDI_REG_TEXT_MAX + 16], *eq, *v, *e;
	for ( ;; ) {
		while ( reg_import && fgets( buf, sizeof(buf), reg_import ) ) {
			strip_eol( buf );
			if ( strnicmp( buf, "HKEY_CLASSES_ROOT\\", 18 ) ||
				( eq = strstr( buf, " =" ) ) == NULL )
				continue;   /* comments, REGEDIT, keys without a value */
			for ( e = eq; e > buf + 18 && e[-1] == ' '; e-- )
				;
			*e = 0;
			for ( v = eq + 2; *v == ' '; v++ )
				;
			sprintf( made, "Set=%s|HKCR|%s|@|SZ|%s", reg_import_cond, buf + 18, v );
			*line = made;
			return pos;
		}
		if ( reg_import ) {
			fclose( reg_import );
			reg_import = NULL;
		}
		if ( ( pos = ini_lines( "Registry", pos, line ) ) < 0 )
			return pos;
		if ( !strnicmp( *line, "Import=", 7 ) ) {
			const char *bar = strchr( *line + 7, '|' );
			char p[80];
			if ( !bar || bar - *line - 7 >= (int)sizeof(reg_import_cond) )
				return -2;
			sprintf( reg_import_cond, "%.*s", (int)( bar - *line - 7 ), *line + 7 );
			path_cat( p, srcdir, bar + 1 );
			if ( !( reg_import = fopen( p, "r" ) ) )
				return -2;
			continue;
		}
		return pos;
	}
}

/* Validate every line, including inactive entries.  This is run before the
 * installer UI can change package files; the final expanded data is checked
 * again while the native plan is written. */
static int registry_preflight( char *message )
{
	const char *line;
	int pos = 0, number = 0;
	while ( ( pos = registry_next( pos, &line ) ) >= 0 ) {
		struct gdi_reg_entry entry;
		char error[100];
		unsigned char data[GDI_REG_DATA_MAX];
		unsigned data_size;
		number++;
		if ( gdi_reg_parse_line( line, &entry, error, sizeof(error) ) ||
		     ( entry.operation == GDI_REG_SET && !strchr( entry.text, '%' ) &&
		       gdi_reg_make_data( &entry, data, &data_size,
		                          error, sizeof(error) ) ) ) {
			sprintf( message, "Registry line %d: %.90s", number, error );
			if ( reg_import ) {
				fclose( reg_import );
				reg_import = NULL;
			}
			return -1;
		}
	}
	if ( pos == -2 ) {
		strcpy( message, "Registry: invalid Import= or missing .REG file" );
		return -1;
	}
	return 0;
}

/* Write only active, expanded entries.  This makes the Windows backend a
 * simple executor: option, detector and path semantics remain owned by the
 * common DOS engine and cannot diverge between Windows families. */
static int registry_write_plan( char *message )
{
	const char *line;
	FILE *file;
	int pos = 0, number = 0;
	char journal[80], error[100];
	if ( !*registry_plan_file ) {
		/* plain DOS: the Windows frontend applies the plan as GDIPxxxx.EXE
		 * when Windows starts the next time */
		int active = 0;
		for ( pos = 0; !active && ( pos = registry_next( pos, &line ) ) >= 0; ) {
			struct gdi_reg_entry entry;
			if ( !gdi_reg_parse_line( line, &entry, error, sizeof(error) ) &&
				condition_true( entry.condition ) )
				active = 1;
		}
		if ( reg_import ) {
			fclose( reg_import );
			reg_import = NULL;
		}
		if ( !active || ( in_extra && !in_batch ) || !*windir )
			return 0;
		path_cat( registry_plan_file, dest, "REGISTRY.PLN" );
		registry_family = 3;
		registry_deferred = 1;
		pos = 0;
	}
	if ( in_extra && !in_batch ) {
		if ( ini_lines( "Registry", 0, &line ) >= 0 ) {
			strcpy( message,
			        "Registry entries in an Extras package are not supported yet" );
			return -1;
		}
		return 0;
	}
	if ( !( file = fopen( registry_plan_file, "w" ) ) ) {
		sprintf( message, "Cannot create registry plan: %.60s", registry_plan_file );
		return -1;
	}
	path_cat( journal, dest, "REGISTRY.LOG" );
	fprintf( file, "GDI-REGISTRY-PLAN 1\nJournal=%s\n", journal );
	while ( ( pos = registry_next( pos, &line ) ) >= 0 ) {
		struct gdi_reg_entry entry;
		char expanded[GDI_REG_TEXT_MAX + 1], output[1100];
		unsigned char data[GDI_REG_DATA_MAX];
		unsigned data_size;
		number++;
		if ( gdi_reg_parse_line( line, &entry, error, sizeof(error) ) )
			goto invalid;
		if ( !condition_true( entry.condition ) )
			continue;
		if ( registry_family == 9 ) {
			strcpy( error, "Windows 9x registry backend is not available yet" );
			goto invalid;
		}
		if ( gdi_reg_validate_family( &entry, registry_family == 9,
		                              error, sizeof(error) ) )
			goto invalid;
		entry.condition[0] = 0;
		if ( expand( expanded, entry.key, sizeof(expanded) ) ) {
			strcpy( error, "expanded registry key is too long" );
			goto invalid;
		}
		if ( strlen( expanded ) > GDI_REG_KEY_MAX ) {
			strcpy( error, "expanded registry key is too long" );
			goto invalid;
		}
		strcpy( entry.key, expanded );
		if ( entry.operation != GDI_REG_DELETE_KEY ) {
			if ( expand( expanded, entry.name, sizeof(expanded) ) ) {
				strcpy( error, "expanded registry value name is too long" );
				goto invalid;
			}
			if ( strlen( expanded ) > GDI_REG_NAME_MAX ) {
				strcpy( error, "expanded registry value name is too long" );
				goto invalid;
			}
			strcpy( entry.name, expanded );
		}
		if ( entry.operation == GDI_REG_SET ) {
			if ( expand( expanded, entry.text, sizeof(expanded) ) ) {
				strcpy( error, "expanded registry data is too long" );
				goto invalid;
			}
			strcpy( entry.text, expanded );
			if ( gdi_reg_make_data( &entry, data, &data_size,
			                        error, sizeof(error) ) )
				goto invalid;
		}
		if ( gdi_reg_format( &entry, output, sizeof(output) ) ) {
			strcpy( error, "expanded registry entry cannot be represented" );
			goto invalid;
		}
		if ( fprintf( file, "%s\n", output ) < 0 ) {
			strcpy( error, "cannot write registry plan" );
			goto invalid;
		}
	}
	if ( pos == -2 ) {
		strcpy( error, "invalid Import= or missing .REG file" );
		goto invalid;
	}
	if ( fclose( file ) ) {
		remove( registry_plan_file );
		strcpy( message, "Cannot finish registry plan" );
		return -1;
	}
	return 0;

invalid:
	if ( reg_import ) {
		fclose( reg_import );
		reg_import = NULL;
	}
	fclose( file );
	remove( registry_plan_file );
	sprintf( message, "Registry line %d: %.90s", number, error );
	return -1;
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

/* the first parent of the target directory that setup creates (DIRS.LOG) */
static char dest_top[80];

static int copy_file( const char *from, const char *to )
{
	FILE *in = fopen( from, "rb" ), *out;
	static char buf[1024];   /* DGROUP is small */
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
	completion_status = 2;
	scr_exit();
	exit( 2 );
}

void gdi_out_of_memory( void )
{
	fail( T( "NoMemory", "Not enough memory." ) );
}

static void abort_setup( void )
{
	static const char *ok[] = { "OK" };
	scr_message( T( "AbortTitle", "Setup" ), T( "Aborted", "Setup was cancelled. Nothing was changed." ), ok, 1, 0, 0 );
	if ( in_extra )
		longjmp( extra_abort, 1 );
	completion_status = 1;
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

/* keep a copy of the original: NAME.G01, NAME.G02, ...; its name goes to
 * saved (if not NULL), "" when there was nothing to keep or it failed */
static void backup_as( const char *path, char *saved )
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

static void old_entries_load( const char *ini )
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
static int entry_new( const char *section, const char *key, const char *v )
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
static const char *cond_new( const char *section, const char *key, const char *v )
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
	path_cat( journal, dest, "SYSTEM.ADD" );
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
			sprintf( msg, "%s %s", T( "WriteError", "Cannot write" ), journal );
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
	path_cat( journal, dest, "SYSTEM.ADD" );
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
static int ini_find_key( const char *section, const char *key, int *end )
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

static int system_set_split( const char *text, char *section, char *key,
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
	path_cat( journal, dest, set_journal( ini_section ) );
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
	path_cat( journal, dest, set_journal( ini_section ) );
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
		if ( !strcmp( key, "Remove" ) ? !update_mode : cond_new( section, key, v ) != NULL )
			return 1;
	return 0;
}

static int has_entry( const char *section, const char *key )
{
	const char *value;
	return ini_next( section, key, 0, &value ) >= 0;
}

/* [HexPatch] Patch=cond|file|old bytes|new bytes|exact count.  The
 * condition is optional, as for other installer entries. */
static char *trim_field( char *text )
{
	char *end;
	while ( *text == ' ' || *text == '\t' )
		text++;
	end = text + strlen( text );
	while ( end > text && ( end[-1] == ' ' || end[-1] == '\t' ) )
		*--end = 0;
	return text;
}

static int patch_split( const char *value, char *path, char *old_hex,
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

typedef struct {
	char group[24], label[24], path[80];
	unsigned char before[HEXPATCH_MAX_BYTES], after[HEXPATCH_MAX_BYTES];
	int length, expected, processed;
} PatchVariant;

/* PatchVariant=group|label|file|old bytes|new bytes|exact count.
 * Entries sharing a group are alternatives; their exact signatures select
 * the language/build without trusting a localized version string. */
static int patch_variant_split( const char *value, PatchVariant *variant )
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

static int hexpatch_needs_windows( void )
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
			fail( T( "PatchDefinition", "Invalid [HexPatch] patch entry." ) );
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
				sprintf( message, "%s %s", T( "PatchError", "Cannot patch" ),
					variants[i].path );
			else
				sprintf( message, "%s %s", T( "PatchMismatch",
					"Binary signature has an unexpected match count in" ),
					variants[i].path );
			fail( message );
		}
		result = patch_one( variants[selected].path,
			variants[selected].before, variants[selected].after,
			variants[selected].length, variants[selected].expected );
		if ( result != HEXPATCH_OK && result != HEXPATCH_ALREADY ) {
			sprintf( message, "%s %s", T( "PatchError", "Cannot patch" ),
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
		path_cat( p, dest, "INSTALL.LOG" );
		if ( ( f = fopen( p, "a" ) ) != NULL ) {
			fprintf( f, "%s\n", saved );
			fclose( f );
		}
	}
	return result;
}

static void patch_all( void )
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
				fail( T( "PatchDefinition", "Invalid [HexPatch] Patch entry." ) );
			old_length = hexpatch_parse( old_hex, old_bytes, sizeof(old_bytes) );
			new_length = hexpatch_parse( new_hex, new_bytes, sizeof(new_bytes) );
			if ( old_length < 1 || old_length != new_length )
				fail( T( "PatchDefinition", "Invalid [HexPatch] Patch entry." ) );
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
				sprintf( message, "%s %s", T( "PatchMismatch", "Binary signature has an unexpected match count in" ), path );
			else
				sprintf( message, "%s %s", T( "PatchError", "Cannot patch" ), path );
			fail( message );
		}
	patch_variants();
}

/* kind: 0 CONFIG.SYS, 1 AUTOEXEC.BAT, 2 SYSTEM.INI, 3 WIN.INI */
static void edit_file( const char *path, const char *section, int kind )
{
	char set_journal_path[80];
	int restored = 0, set_journal_exists = 0, unadded = 0;
	if ( kind >= 2 && uninstall ) {
		path_cat( set_journal_path, dest, set_journal( section ) );
		set_journal_exists = file_exists( set_journal_path );
		if ( kind == 2 ) {
			char added[80];
			path_cat( added, dest, "SYSTEM.ADD" );
			set_journal_exists |= file_exists( added );
		}
	}
	if ( !has_active( section, "Add" ) && !has_active( section, "Remove" ) &&
		!has_active( section, "Extend" ) &&
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
			fail( T( "SetRestoreError", "Cannot restore SYSTEM.INI settings." ) );
		if ( kind == 2 )
			unadded = ed_unadd_ini();
	}
	if ( !update_mode ) {   /* an update only adds the new entries */
		ed_remove( section );
		if ( kind < 2 )
			ed_unextend( section );
	}
	if ( !uninstall ) {
		if ( kind == 2 )
			ed_add_ini();
		else if ( kind < 2 )
			ed_add_dos( section, kind == 0 );
	}
	if ( kind >= 2 && !uninstall && ed_set_ini( section ) )
		fail( T( "SetDefinition", "Invalid [System.ini] Set entry." ) );
	if ( ed_save( path ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "WriteError", "Cannot write" ), path );
		fail( msg );
	}
	if ( restored > 0 )
		remove( set_journal_path );
	if ( unadded ) {
		path_cat( set_journal_path, dest, "SYSTEM.ADD" );
		remove( set_journal_path );
	}
	ed_free();
}

/* --- Windows tasks: WIN.INI run=, Program Manager, the frontend's plan --- */

/* the engine was started by the Win16 frontend: WIN.INI and the groups are
 * changed by the frontend (Windows keeps them in memory while it runs) */
static int frontend( void )
{
	return *completion_file != 0;
}

/* Windows runs (enhanced mode, or standard / real mode of Windows 3.x) */
static int windows_running( void )
{
	union REGS r;
	r.x.ax = 0x1600;
	int86( 0x2F, &r, &r );
	if ( r.h.al != 0 && r.h.al != 0x80 )
		return 1;
	r.x.ax = 0x4680;
	int86( 0x2F, &r, &r );
	return r.x.ax == 0;
}

/* add (1) or remove (0) a program in run= of [windows] in WIN.INI */
static void winini_run( int add, const char *program )
{
	char path[80], value[256], out[256], word[80];
	const char *v;
	int index, end, found = 0, n;
	path_cat( path, windir, "WIN.INI" );
	if ( !*windir || ( ed_load( path ) && !add ) )
		return;
	index = ini_find_key( "windows", "run", &end );
	if ( index == -2 )
		fail( T( "SetDefinition", "Invalid [System.ini] Set entry." ) );
	value[0] = 0;
	if ( index >= 0 && ( v = strchr( ed[index], '=' ) ) != NULL )
		sprintf( value, "%.255s", v + 1 );
	strcpy( out, "run=" );
	for ( v = value; *v; ) {
		while ( *v == ' ' || *v == '\t' || *v == ',' )
			v++;
		for ( n = 0; v[n] && v[n] != ' ' && v[n] != '\t' && v[n] != ','; n++ );
		if ( !n )
			break;
		sprintf( word, "%.*s", n < 79 ? n : 79, v );
		v += n;
		if ( !stricmp( word, program ) ) {
			found = 1;
			if ( !add )
				continue;
		}
		if ( strlen( out ) + strlen( word ) + 2 < sizeof(out) ) {
			if ( out[4] )
				strcat( out, " " );
			strcat( out, word );
		}
	}
	if ( found == add ) {
		ed_free();
		return;
	}
	if ( add && strlen( out ) + strlen( program ) + 2 < sizeof(out) ) {
		if ( out[4] )
			strcat( out, " " );
		strcat( out, program );
	}
	if ( index >= 0 ) {
		free( ed[index] );
		ed[index] = str_dup( out );
	} else if ( end >= 0 ) {
		while ( end > 0 && !ed[end - 1][0] )
			end--;
		ed_insert( end, out );
	} else {
		ed_insert( 0, "[windows]" );
		ed_insert( 1, out );
		ed_insert( 2, "" );
	}
	if ( ed_save( path ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "WriteError", "Cannot write" ), path );
		fail( msg );
	}
	ed_free();
}

/* [ProgMan] Item=cond|Group|Title|Command[|Icon file[|Icon index]]: fields
 * expanded and converted to ANSI; 1 = condition false, -1 = invalid */
static int progman_item( const char *value, char *group, char *title,
	char *command, char *icon, int *index )
{
	static char data[300];
	char *field[6], *p;
	int n = 1;
	if ( strlen( value ) >= sizeof(data) )
		return -1;
	strcpy( data, value );
	field[0] = data;
	for ( p = data; *p && n < 6; p++ )
		if ( *p == '|' ) {
			*p = 0;
			field[n++] = p + 1;
		}
	if ( n < 4 || strchr( p, '|' ) )
		return -1;
	if ( strlen( trim_field( field[0] ) ) > GDI_REG_CONDITION_MAX )
		return -1;
	if ( !condition_true( trim_field( field[0] ) ) )
		return 1;
	sprintf( group, "%.79s", trim_field( field[1] ) );
	sprintf( title, "%.79s", trim_field( field[2] ) );
	expand( command, trim_field( field[3] ), 128 );
	if ( n > 4 && *trim_field( field[4] ) )
		expand( icon, trim_field( field[4] ), 128 );
	else {   /* the program of the command line */
		sprintf( icon, "%.127s", command );
		if ( ( p = strchr( icon, ' ' ) ) != NULL )
			*p = 0;
	}
	*index = n > 5 ? atoi( trim_field( field[5] ) ) : 0;
	if ( !*group || !*title || !*command )
		return -1;
	gdi_pm_oem_to_ansi( group );
	gdi_pm_oem_to_ansi( title );
	gdi_pm_oem_to_ansi( command );
	gdi_pm_oem_to_ansi( icon );
	return 0;
}

static int has_progman( void )
{
	return has_entry( "ProgMan", "Item" );
}

/* plain DOS: the groups are changed in their files; PROGMAN.LOG records
 * "G|group" (created) and "I|group|title" (added) for INSTALL /U */
static void progman_all( void )
{
	static char group[80], title[80], command[128], icon[128];
	const char *v;
	char log[80], msg[160];
	FILE *f = NULL;
	int pos = 0, rc, created, index;
	if ( frontend() || !*windir )
		return;
	path_cat( log, dest, "PROGMAN.LOG" );
	while ( ( pos = ini_next( "ProgMan", "Item", pos, &v ) ) >= 0 ) {
		if ( !entry_new( "ProgMan", "Item", v ) )
			continue;
		rc = progman_item( v, group, title, command, icon, &index );
		if ( rc < 0 )
			fail( T( "ProgManDefinition", "Invalid [ProgMan] Item entry." ) );
		if ( rc )
			continue;
		rc = gdi_pm_add_item( windir, group, title, command, icon, index, &created );
		if ( rc < 0 ) {
			sprintf( msg, "%s %.80s", T( "ProgManError", "Cannot change the Program Manager group" ), group );
			fail( msg );
		}
		if ( rc )
			continue;   /* already there */
		if ( !f && !( f = fopen( log, "a" ) ) ) {
			sprintf( msg, "%s %s", T( "WriteError", "Cannot write" ), log );
			fail( msg );
		}
		if ( created )
			fprintf( f, "G|%s\n", group );
		fprintf( f, "I|%s|%s\n", group, title );
	}
	if ( f )
		fclose( f );
}

/* INSTALL /U in plain DOS: take the items and groups out again */
static void progman_undo( void )
{
	char log[80], l[180], *bar;
	FILE *f;
	int pass;
	if ( frontend() || !*windir )
		return;
	path_cat( log, dest, "PROGMAN.LOG" );
	for ( pass = 0; pass < 2; pass++ ) {   /* items, then empty groups */
		if ( !( f = fopen( log, "r" ) ) )
			return;
		while ( fgets( l, sizeof(l), f ) ) {
			strip_eol( l );
			if ( pass == 0 && l[0] == 'I' && l[1] == '|' && ( bar = strchr( l + 2, '|' ) ) != NULL ) {
				*bar = 0;
				if ( gdi_pm_remove_item( windir, l + 2, bar + 1 ) < 0 )
					fail( T( "ProgManError", "Cannot change the Program Manager group" ) );
			}
			if ( pass == 1 && l[0] == 'G' && l[1] == '|' &&
				gdi_pm_remove_group( windir, l + 2 ) < 0 )
				fail( T( "ProgManError", "Cannot change the Program Manager group" ) );
		}
		fclose( f );
	}
	remove( log );
}

/* started by the frontend: the WIN.INI values and the items it applies,
 * "GDI-TASK-PLAN 1", Dir=, WinDir=, then "W|section|key=value",
 * "P|group|title|command|icon|index", "R|runner" and "U|runner" (run=) and
 * "X|program" ([Run] Windows=) (expanded, ANSI) */
static int task_plan_started;

static void task_write_plan( void )
{
	static char group[80], title[80], command[128], icon[128];
	char section[40], key[80], line[256];
	const char *v, *t;
	FILE *f;
	int pos, index, rc;
	if ( !*task_plan_file || ( in_extra && !in_batch ) )
		return;
	/* [Installer]: one part (Dir=) per package */
	if ( !( f = fopen( task_plan_file, task_plan_started ? "a" : "w" ) ) )
		fail( T( "TaskPlanError", "Cannot write the plan for Windows." ) );
	if ( !task_plan_started )
		fprintf( f, "GDI-TASK-PLAN 1\n" );
	task_plan_started = 1;
	fprintf( f, "Dir=%s\nWinDir=%s\n", dest, windir );
	if ( *windir ) {
		for ( pos = 0; ( pos = ini_next( "Win.ini", "Set", pos, &v ) ) >= 0; ) {
			if ( !( t = cond_new( "Win.ini", "Set", v ) ) )
				continue;
			if ( system_set_split( t, section, key, line ) )
				fail( T( "SetDefinition", "Invalid [System.ini] Set entry." ) );
			gdi_pm_oem_to_ansi( line );
			fprintf( f, "W|%s|%s\n", section, line );
		}
		for ( pos = 0; ( pos = ini_next( "ProgMan", "Item", pos, &v ) ) >= 0; ) {
			rc = progman_item( v, group, title, command, icon, &index );
			if ( rc < 0 )
				fail( T( "ProgManDefinition", "Invalid [ProgMan] Item entry." ) );
			if ( !rc && entry_new( "ProgMan", "Item", v ) )
				fprintf( f, "P|%s|%s|%s|%s|%d\n", group, title, command, icon, index );
		}
	}
	if ( fclose( f ) )
		fail( T( "TaskPlanError", "Cannot write the plan for Windows." ) );
}

static void task_plan_line( char kind, const char *text )
{
	char line[160];
	FILE *f = fopen( task_plan_file, task_plan_started ? "a" : "w" );
	if ( !f )
		fail( T( "TaskPlanError", "Cannot write the plan for Windows." ) );
	if ( !task_plan_started )
		fprintf( f, "GDI-TASK-PLAN 1\nDir=%s\nWinDir=%s\n", dest, windir );
	task_plan_started = 1;
	sprintf( line, "%.159s", text );
	gdi_pm_oem_to_ansi( line );
	fprintf( f, "%c|%s\n", kind, line );
	if ( fclose( f ) )
		fail( T( "TaskPlanError", "Cannot write the plan for Windows." ) );
}

/* the copy of this installer that does the registry work at the next start
 * of Windows (run= in WIN.INI).  Windows 3.x starts a file name only once (a
 * second GDIPOST.EXE would become an instance of the first, with its file
 * name), so each package directory gets its own: GDIPxxxx.EXE */
static void runner_path( char *p )
{
	unsigned hash = 0;
	const char *c;
	char name[16];
	for ( c = dest; *c; c++ )
		hash = hash * 31 + (unsigned char)toupper( (unsigned char)*c );
	sprintf( name, "GDIP%04X.EXE", hash );
	path_cat( p, dest, name );
}

/* plain DOS with registry entries: the runner (this installer) applies
 * REGISTRY.PLN, or restores REGISTRY.LOG after INSTALL /U (UNINST.PND),
 * when Windows starts the next time (run= in WIN.INI) */
static void post_runner( int add )
{
	char from[80], to[80];
	runner_path( to );
	if ( add ) {
		path_cat( from, srcdir, "INSTALL.EXE" );
		if ( stricmp( from, to ) && copy_file( from, to ) ) {
			char msg[160];
			sprintf( msg, "%s %s -> %s", T( "CopyError", "Cannot copy" ), from, to );
			fail( msg );
		}
	}
	if ( frontend() && *task_plan_file )
		task_plan_line( add ? 'R' : 'U', to );   /* the frontend changes run= */
	else
		winini_run( add, to );
	if ( !add )
		remove( to );
}

/* the runner of this package directory, in run= and INSTALL.LOG */
static void add_runner( void )
{
	static char added_for[80];
	char p[80];
	FILE *f;
	if ( !stricmp( added_for, dest ) )
		return;
	strcpy( added_for, dest );
	post_runner( 1 );
	path_cat( p, dest, "INSTALL.LOG" );   /* INSTALL /U removes it */
	if ( ( f = fopen( p, "a" ) ) != NULL ) {
		runner_path( p );
		fprintf( f, "%s\n", p );
		fclose( f );
	}
}

static void deferred_registry( void )
{
	if ( registry_deferred )
		add_runner();
}

/* [Run] Windows=cond|program: started once under Windows after setup, by
 * the frontend right away, from plain DOS by the runner at the next start
 * of Windows (RUNONCE.PLN) */
static void run_windows( void )
{
	const char *v, *t;
	char command[160], p[80];
	FILE *f = NULL;
	int pos = 0;
	if ( !*windir )
		return;
	while ( ( pos = ini_next( "Run", "Windows", pos, &v ) ) >= 0 ) {
		if ( ( t = cond( v ) ) == NULL )
			continue;
		expand( command, t, sizeof(command) );
		if ( frontend() && *task_plan_file ) {
			task_plan_line( 'X', command );
			continue;
		}
		if ( !f ) {
			path_cat( p, dest, "RUNONCE.PLN" );
			if ( !( f = fopen( p, "w" ) ) )
				fail( T( "WriteError", "Cannot write" ) );
		}
		fprintf( f, "%s\n", command );
	}
	if ( f ) {
		if ( fclose( f ) )
			fail( T( "WriteError", "Cannot write" ) );
		add_runner();
	}
}

/* INSTALL /U in plain DOS: 1 = the registry is restored later by the runner */
static int uninstall_registry( void )
{
	char p[80];
	FILE *f;
	path_cat( p, dest, "RUNONCE.PLN" );
	remove( p );   /* [Run] Windows= never started */
	if ( frontend() )
		return 0;
	path_cat( p, dest, "REGISTRY.PLN" );
	remove( p );   /* never applied */
	path_cat( p, dest, "REGISTRY.LOG" );
	if ( !file_exists( p ) ) {
		if ( *windir )
			post_runner( 0 );
		return 0;
	}
	path_cat( p, dest, "UNINST.PND" );
	if ( !( f = fopen( p, "w" ) ) || fclose( f ) )
		fail( T( "WriteError", "Cannot write" ) );
	post_runner( 1 );
	return 1;
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

/* Run a command line and return its exit status.  A program without a path
 * is taken from the directory containing INSTALL.EXE. */
static int spawn_program( const char *cmdline )
{
	char line[200], prog[80], *argv[16];
	int argc = 0;
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
	return spawnv( P_WAIT, prog, (const char * const *)argv );
}

/* [Detect] NAME=program arguments: NAME becomes true when the external
 * program returns zero.  The installer has no knowledge of what it tests. */
static void detect_all( void )
{
	const char *v;
	int pos = 0;
	ndetect = 0;
	while ( ndetect < MAXDETECT && ( pos = ini_text( "Detect", pos, &v ) ) >= 0 ) {
		const char *eq = strchr( v, '=' );
		int n;
		if ( !eq )
			continue;
		n = eq - v;
		while ( n && ( v[n - 1] == ' ' || v[n - 1] == '\t' ) )
			n--;
		if ( !n || n > 15 )
			continue;
		sprintf( detects[ndetect].name, "%.*s", n, v );
		while ( eq[1] == ' ' || eq[1] == '\t' )
			eq++;
		detects[ndetect].result = spawn_program( eq + 1 ) == 0;
		ndetect++;
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
	char p[80];
	ftext *l;
	int nl, rc;
	path_cat( p, srcdir, file );
	if ( !file_exists( p ) && *dest )   /* the disk may have been changed */
		path_cat( p, dest, file );
	if ( !( l = text_load( p, &nl ) ) ) {
		char msg[120];
		sprintf( msg, "%s %s", file_exists( p ) ? T( "NoMemory", "Not enough memory for" )
		                                        : T( "MissingFile", "File not found:" ), p );
		fail( msg );
	}
	scr_keys( T( "KeysView", "PgUp=Page up  PgDn=Page down  Enter=Select  Esc=Abort" ) );
	rc = scr_view( title, l, nl, buttons, n, 0 );
	text_free( l, nl );
	if ( must && rc != 0 )
		abort_setup();
}

/* licenses accepted in this run: the same license of several packages is
 * shown once.  A license is known by its name (License=Name|file), one
 * without a name by the length and sum of its text. */
#define MAXACCEPTED 32
static long accepted_len[MAXACCEPTED], accepted_sum[MAXACCEPTED];
static char accepted_name[MAXACCEPTED][45];
static int naccepted;

static int license_id( const char *file, long *len, long *sum )
{
	char p[80];
	FILE *f;
	int c;
	path_cat( p, srcdir, file );
	if ( !( f = fopen( p, "rb" ) ) )
		return -1;
	*len = *sum = 0;
	while ( ( c = getc( f ) ) != EOF ) {
		++*len;
		*sum = *sum * 31 + c;
	}
	fclose( f );
	return 0;
}

static void show_license( const char *value, const char **buttons )
{
	const char *bar = strchr( value, '|' );
	char file[80], title[120], name_buf[45];
	long len = 0, sum = 0;
	int i, known;
	name_buf[0] = 0;
	if ( bar ) {
		const char *name = value;
		const char *end = bar;
		int name_len;
		while ( name < end && ( *name == ' ' || *name == '\t' ) )
			name++;
		while ( end > name && ( end[-1] == ' ' || end[-1] == '\t' ) )
			end--;
		while ( bar[1] == ' ' || bar[1] == '\t' )
			bar++;
		sprintf( file, "%.79s", bar + 1 );
		name_len = end - name;
		if ( name_len > 44 )
			name_len = 44;
		sprintf( name_buf, "%.*s", name_len, name );
		if ( name_len )
			sprintf( title, "%.70s - %.*s",
				T( "LicenseTitle", "License agreement" ), name_len, name );
		else
			sprintf( title, "%.119s", T( "LicenseTitle", "License agreement" ) );
	} else {
		sprintf( file, "%.79s", value );
		sprintf( title, "%.119s", T( "LicenseTitle", "License agreement" ) );
	}
	known = *name_buf || license_id( file, &len, &sum ) == 0;
	for ( i = 0; known && i < naccepted; i++ )
		if ( *name_buf ? !stricmp( accepted_name[i], name_buf ) :
			!accepted_name[i][0] && accepted_len[i] == len && accepted_sum[i] == sum )
			return;   /* accepted already */
	show_text( title, file, buttons, 2, 1 );
	scr_desktop();
	if ( known && naccepted < MAXACCEPTED ) {
		strcpy( accepted_name[naccepted], name_buf );
		accepted_len[naccepted] = len;
		accepted_sum[naccepted++] = sum;
	}
}

/* A comma-separated number list selects License1=, License2=, ... . */
static int is_license_list( const char *value )
{
	const char *p = value;
	for ( ;; ) {
		while ( *p == ' ' || *p == '\t' )
			p++;
		if ( !isdigit( *p ) )
			return 0;
		while ( isdigit( *p ) )
			p++;
		while ( *p == ' ' || *p == '\t' )
			p++;
		if ( !*p )
			return 1;
		if ( *p++ != ',' )
			return 0;
	}
}

static void license( void )
{
	const char *value;
	static const char *b[2];
	int pos = 0, count = 0;
	b[0] = T( "Accept", "I accept" );
	b[1] = T( "Decline", "Decline" );
	while ( ( pos = ini_next( "Setup", "License", pos, &value ) ) >= 0 ) {
		if ( is_license_list( value ) ) {
			const char *p = value;
			while ( *p ) {
				char key[20], msg[120];
				int number = 0;
				while ( *p == ' ' || *p == '\t' || *p == ',' )
					p++;
				if ( !*p )
					break;
				while ( isdigit( *p ) ) {
					number = number * 10 + *p - '0';
					p++;
				}
				if ( number < 1 || number > 9 )
					fail( T( "LicenseRange", "License numbers must be between 1 and 9." ) );
				if ( ++count > 9 )
					fail( T( "LicenseLimit", "A package may contain at most 9 licenses." ) );
				sprintf( key, "License%d", number );
				value = ini_get( "Setup", key, NULL );
				if ( !value ) {
					sprintf( msg, "%s %s", T( "MissingLicense", "License entry not found:" ), key );
					fail( msg );
				}
				show_license( value, b );
			}
		} else {
			if ( ++count > 9 )
				fail( T( "LicenseLimit", "A package may contain at most 9 licenses." ) );
			show_license( value, b );
		}
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
		/* Option=on|Label|value or ?NAME|Label: %On% expands to value */
		const char *bar = strchr( v, '|' ), *bar2;
		static char labels[MAXOPT][70];
		if ( *v == '?' && bar )
			opt_on[nopt] = named_result( v + 1, bar - v - 1 );
		else if ( *v == '!' && v[1] == '?' && bar )
			opt_on[nopt] = !named_result( v + 2, bar - v - 2 );
		else
			opt_on[nopt] = !strnicmp( v, "on", 2 ) || *v == '1' || toupper( *v ) == 'Y';
		if ( option_override && nopt < (int)strlen( option_override ) ) {
			if ( option_override[nopt] == '0' )
				opt_on[nopt] = 0;
			else if ( option_override[nopt] == '1' )
				opt_on[nopt] = 1;
		}
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
	char title[90];
	if ( !nopt )
		return;
	sprintf( title, "%.40s", T( "OptionsTitle", "Options" ) );
	if ( in_batch )   /* [Installer]: which package */
		sprintf( title + strlen( title ), " - %.45s", ini_get( "Setup", "Product", "" ) );
	scr_keys( T( "KeysOpt", "Space=On/off  Enter=Continue  Esc=Abort" ) );
	if ( scr_checklist( title,
		T( "OptionsText", "Select the parts to install (Space switches on/off):" ), opt_label, opt_on, nopt ) )
		abort_setup();
}

static void ask_dirs( void )
{
	const char *value;
	strcpy( dest, ini_get( "Setup", "DefaultDir", "C:\\PROGRAM" ) );
	expand( dest, dest, sizeof(dest) );
	if ( in_batch ) {   /* [Installer]: the default directory, no questions */
		gdi_note_parents( dest, dest_top );
		if ( mkdirs( dest ) )
			fail( T( "DirError", "Please enter a full path (e.g. C:\\DRIVERS) that can be created." ) );
		find_dest();
		return;
	}
	scr_keys( T( "KeysInput", "Enter=Continue  Esc=Abort" ) );
	for ( ;; ) {
		if ( scr_input( T( "DirTitle", "Destination" ), T( "DirText", "Install the files to this directory:" ), dest, 64 ) )
			abort_setup();
		gdi_note_parents( dest, dest_top );
		if ( strlen( dest ) >= 3 && dest[1] == ':' && dest[2] == '\\' && !mkdirs( dest ) )
			break;
		{
			static const char *ok[] = { "OK" };
			scr_message( T( "ErrorTitle", "Error" ), T( "DirError", "Please enter a full path (e.g. C:\\DRIVERS) that can be created." ), ok, 1, 0, 1 );
		}
	}
	find_dest();
	if ( has_entry( "System.ini", "Add" ) || has_entry( "System.ini", "Set" ) ||
		has_entry( "Win.ini", "Set" ) || has_progman() ||
		ini_lines( "Registry", 0, &value ) >= 0 || hexpatch_needs_windows() ) {
		find_windows();
		if ( scr_input( T( "WinTitle", "Windows" ),
			T( "WinText", "Windows directory for the SYSTEM.INI entries (empty: no Windows):" ), windir, 64 ) )
			abort_setup();
	}
}

static int confirm( void )
{
	static const char *patch_keys[] = { "Patch", "PatchIfPresent" };
	static char text[1500];
	static const char *b[2];
	const char *v, *t;
	char line[160];
	int key, pos;
	b[0] = T( "Install", "Install" );
	b[1] = T( "Cancel", "Cancel" );
	if ( update_mode )
		b[0] = T( "Update", "Update" );
	sprintf( text, "%s %s\n", update_mode ? T( "SumUpdateFiles", "Update the files in:" ) :
		T( "SumFiles", "Files to:" ), dest );
	pos = 0;
	while ( ( pos = ini_next( "Config.sys", "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond_new( "Config.sys", "Add", v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			if ( strlen( text ) + strlen( line ) + 40 < sizeof(text) )
				sprintf( text + strlen( text ), "%s\\CONFIG.SYS: %s\n", boot, line );
		}
	pos = 0;
	while ( ( pos = ini_next( "Autoexec.bat", "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond_new( "Autoexec.bat", "Add", v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			if ( strlen( text ) + strlen( line ) + 40 < sizeof(text) )
				sprintf( text + strlen( text ), "%s\\AUTOEXEC.BAT: %s\n", boot, line );
		}
	pos = 0;   /* Extend: the text to append (or the new line) */
	while ( ( pos = ini_next( "Autoexec.bat", "Extend", pos, &v ) ) >= 0 ) {
		char m[256], x[256], n[256];
		if ( ( t = cond_new( "Autoexec.bat", "Extend", v ) ) != NULL && !ext_split( t, m, x, n ) && strlen( text ) + strlen( x ) + 60 < sizeof(text) )
			sprintf( text + strlen( text ), "%s\\AUTOEXEC.BAT: %s ... %s\n", boot, m, x );
	}
	if ( windir[0] ) {
		pos = 0;
		while ( ( pos = ini_next( "System.ini", "Add", pos, &v ) ) >= 0 )
			if ( ( t = cond_new( "System.ini", "Add", v ) ) != NULL && strchr( t, '|' ) ) {
				expand( line, strchr( t, '|' ) + 1, sizeof(line) );
				sprintf( text + strlen( text ), "SYSTEM.INI [%.*s]: %s\n", (int)( strchr( t, '|' ) - t ), t, line );
			}
		pos = 0;
		while ( ( pos = ini_next( "System.ini", "Set", pos, &v ) ) >= 0 )
			if ( ( t = cond_new( "System.ini", "Set", v ) ) != NULL && strchr( t, '|' ) ) {
				expand( line, strchr( t, '|' ) + 1, sizeof(line) );
				if ( strlen( text ) + strlen( line ) + 40 < sizeof(text) )
					sprintf( text + strlen( text ), "SYSTEM.INI [%.*s]: %s\n",
						(int)( strchr( t, '|' ) - t ), t, line );
			}
	}
	if ( windir[0] ) {
		pos = 0;
		while ( ( pos = ini_next( "Win.ini", "Set", pos, &v ) ) >= 0 )
			if ( ( t = cond_new( "Win.ini", "Set", v ) ) != NULL && strchr( t, '|' ) ) {
				expand( line, strchr( t, '|' ) + 1, sizeof(line) );
				if ( strlen( text ) + strlen( line ) + 40 < sizeof(text) )
					sprintf( text + strlen( text ), "WIN.INI [%.*s]: %s\n",
						(int)( strchr( t, '|' ) - t ), t, line );
			}
		pos = 0;
		while ( ( pos = ini_next( "ProgMan", "Item", pos, &v ) ) >= 0 ) {
			static char group[80], title[80], command[128], icon[128];
			int index;
			if ( entry_new( "ProgMan", "Item", v ) &&
				!progman_item( v, group, title, command, icon, &index ) &&
				strlen( text ) + strlen( group ) + strlen( title ) + 40 < sizeof(text) )
				sprintf( text + strlen( text ), "%s %s: %s\n",
					T( "SumProgMan", "Program Manager" ), group, title );
		}
	}
	for ( key = 0; key < ( update_mode ? 0 : 2 ); key++ )
		for ( pos = 0; ( pos = ini_next( "HexPatch", patch_keys[key], pos,
			&v ) ) >= 0; )
			if ( ( t = cond( v ) ) != NULL ) {
				char path[80], old_hex[96], new_hex[96];
				int expected;
				if ( !patch_split( t, path, old_hex, new_hex, &expected ) &&
					( key == 0 || file_exists( path ) ) &&
					strlen( text ) + strlen( path ) + 20 < sizeof(text) )
					sprintf( text + strlen( text ), "%s: %s\n",
						T( "SumPatch", "Binary patch" ), path );
			}
	pos = 0;
	while ( !update_mode && ( pos = ini_next( "HexPatch", "PatchVariant", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			PatchVariant variant;
			if ( !patch_variant_split( t, &variant ) &&
				strlen( text ) + strlen( variant.path ) + 40 < sizeof(text) )
				sprintf( text + strlen( text ), "%s (%s): %s\n",
					T( "SumPatch", "Binary patch" ), variant.label, variant.path );
		}
	if ( update_mode ) {
		strcat( text, "\n" );
		strcat( text, T( "SumUpdateKeep", "Existing entries and the settings files of the program stay as they are." ) );
	} else
		strcat( text, T( "SumBackup", "\nThe original files are kept as *.G01, *.G02, ..." ) );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	return scr_message( update_mode ? T( "SumUpdateTitle", "Ready to update" ) :
		T( "SumTitle", "Ready to install" ), text, b, 2, 0, 0 ) == 0;
}

/* is %NAME% used in an active line of the configuration sections? */
static int find_used( const char *name )
{
	static const char *sect[] = { "Config.sys", "Autoexec.bat", "System.ini" };
	static const char *keys[] = { "Add", "Extend" };
	static const char *patch_keys[] = { "Patch", "PatchIfPresent",
		"PatchVariant" };
	char var[20];
	const char *v, *t;
	int s, k, pos;
	sprintf( var, "%%%s%%", name );
	for ( s = 0; s < 3; s++ )
		for ( k = 0; k < 2; k++ )
			for ( pos = 0; ( pos = ini_next( sect[s], keys[k], pos, &v ) ) >= 0; )
				if ( ( t = cond( v ) ) != NULL && contains( t, var ) )
					return 1;
	for ( pos = 0; ( pos = ini_next( "System.ini", "Set", pos, &v ) ) >= 0; )
		if ( ( t = cond( v ) ) != NULL && contains( t, var ) )
			return 1;
	for ( k = 0; k < 3; k++ )
		for ( pos = 0; ( pos = ini_next( "HexPatch", patch_keys[k], pos,
			&v ) ) >= 0; )
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

/* --- updates ------------------------------------------------------------ */

#define MAXOLD 800
static char __far *old_log[MAXOLD];   /* far heap: a log can be long */
static int nold;


/* %BOOT%\GDI.INI [Installed] lists every installation that can be removed
 * again (Uninstall=yes): Id=directory|options|version.  Setup finds an
 * earlier installation of the package there and updates it: only the files
 * are replaced.  CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI, WIN.INI, the groups,
 * the registry and the package's settings files ([Keep]) stay as they are. */

static int record_read_key( const char *key, char *dir, char *bits, char *version )
{
	return gdi_record_read( boot, key, dir, bits, version );
}

static int record_read( char *dir, char *bits, char *version )
{
	char key[64];
	ini_package_id( key, sizeof(key) );
	return record_read_key( key, dir, bits, version );
}

/* enter (add) or remove this installation in GDI.INI */
static void record_write( int add )
{
	char key[64], value[120], bits[MAXOPT + 1];
	int i;
	ini_package_id( key, sizeof(key) );
	for ( i = 0; i < nopt; i++ )
		bits[i] = opt_on[i] ? '1' : '0';
	bits[nopt] = 0;
	sprintf( value, "%s|%s|%.23s", dest, bits, ini_get( "Setup", "Version", "" ) );
	gdi_record_write( boot, key, add ? value : NULL );
}

/* an earlier installation of this package: ask, then update it */
static void find_update( void )
{
	static const char *b[2];
	static char text[500];
	char dir[80], bits[MAXOPT + 1], p[80];
	const char *version = ini_get( "Setup", "Version", "" );
	int i;
	update_mode = 0;
	if ( dosmode || stricmp( ini_get( "Setup", "Uninstall", "yes" ), "yes" ) ||
		!record_read( dir, bits, old_version ) )
		return;
	path_cat( p, dir, "INSTALL.INI" );
	if ( !file_exists( p ) )
		return;
	sprintf( text, T( "UpdateText", "%s is already installed in %s. Setup replaces its files; the system settings and the settings files of the program stay as they are." ),
		ini_get( "Setup", "Product", "The software" ), dir );
	if ( *old_version || *version )
		sprintf( text + strlen( text ), "\n\n%s %s\n%s %s",
			T( "UpdateOld", "Installed version:" ), *old_version ? old_version : "?",
			T( "UpdateNew", "New version:" ), *version ? version : "?" );
	b[0] = T( "Update", "Update" );
	b[1] = T( "Cancel", "Cancel" );
	scr_desktop();
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	if ( !in_batch && scr_message( T( "UpdateTitle", "Update" ), text, b, 2, 0, 0 ) != 0 )
		abort_setup();
	update_mode = 1;
	strcpy( dest, dir );
	find_dest();
	old_entries_load( p );   /* before the new INSTALL.INI replaces it */
	for ( i = 0; i < nopt && bits[i]; i++ )
		opt_on[i] = bits[i] == '1';
}

static int wild_match( const char *p, const char *s )
{
	for ( ; *p; p++, s++ ) {
		if ( *p == '*' ) {
			for ( ;; s++ ) {
				if ( wild_match( p + 1, s ) )
					return 1;
				if ( !*s )
					return 0;
			}
		}
		if ( !*s || ( *p != '?' && toupper( (unsigned char)*p ) != toupper( (unsigned char)*s ) ) )
			return 0;
	}
	return !*s;
}

static int old_log_find( const char *path );

/* update: an existing settings file stays ([Keep] cond|pattern; a pattern
 * without a path is a file name, else a path below %DIR% or a full path;
 * without [Keep]: *.INI and *.CFG).  Its log line is kept. */
static int keep_file( const char *path )
{
	const char *v, *t, *name = strrchr( path, '\\' );
	char pat[80];
	int pos = 0, n = strlen( dest ), any = 0, keep = 0, i;
	name = name ? name + 1 : path;
	if ( !update_mode || !file_exists( path ) )
		return 0;
	while ( !keep && ( pos = ini_lines( "Keep", pos, &v ) ) >= 0 ) {
		any = 1;
		if ( ( t = cond( v ) ) == NULL )
			continue;
		expand( pat, t, sizeof(pat) );
		if ( pat[1] == ':' )
			keep = wild_match( pat, path );
		else if ( strchr( pat, '\\' ) )
			keep = !strnicmp( path, dest, n ) && path[n] == '\\' && wild_match( pat, path + n + 1 );
		else
			keep = wild_match( pat, name );
	}
	if ( !any )
		keep = wild_match( "*.INI", name ) || wild_match( "*.CFG", name );
	if ( keep && logf && ( i = old_log_find( path ) ) >= 0 ) {
		fprintf( logf, "%Fs\n", old_log[i] );
		_ffree( old_log[i] );
		old_log[i] = NULL;
	}
	return keep;
}

/* INSTALL.LOG: one line per installed file, "path|backup" when an existing
 * file was replaced (INSTALL /U puts the backup back).  The lines of an
 * earlier installation into the same directory are kept: a file installed
 * again keeps its first backup, so the original is restored. */
static void old_log_load( const char *path )
{
	FILE *f = fopen( path, "r" );
	char l[180];
	nold = 0;
	if ( !f )
		return;
	while ( nold < MAXOLD && fgets( l, sizeof(l), f ) ) {
		char __far *p;
		strip_eol( l );
		if ( !*l || !( p = _fmalloc( strlen( l ) + 1 ) ) )
			continue;
		_fstrcpy( p, l );
		old_log[nold++] = p;
	}
	fclose( f );
}

/* the old line for path (up to '|'), or -1 */
static int old_log_find( const char *path )
{
	int i, n = strlen( path );
	for ( i = 0; i < nold; i++ ) {
		const char __far *l = old_log[i];
		int k;
		if ( !l )
			continue;
		for ( k = 0; k < n && l[k] && toupper( (unsigned char)l[k] ) == toupper( (unsigned char)path[k] ); k++ );
		if ( k == n && ( !l[k] || l[k] == '|' ) )
			return i;
	}
	return -1;
}

/* the lines of the old log that were not installed again */
static void old_log_flush( void )
{
	int i;
	for ( i = 0; i < nold; i++ )
		if ( old_log[i] ) {
			if ( logf )
				fprintf( logf, "%Fs\n", old_log[i] );
			_ffree( old_log[i] );
			old_log[i] = NULL;
		}
	nold = 0;
}

/* before a file is written: log it, keep the file it replaces */
static void log_file( const char *path )
{
	char saved[80];
	int i;
	if ( !logf )
		return;
	if ( ( i = old_log_find( path ) ) >= 0 ) {
		fprintf( logf, "%Fs\n", old_log[i] );
		_ffree( old_log[i] );
		old_log[i] = NULL;
		return;
	}
	backup_as( path, saved );
	if ( *saved )
		fprintf( logf, "%s|%s\n", path, saved );
	else
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
			if ( keep_file( path ) )
				return -1;
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
		old_log_load( to );
		logf = fopen( to, "w" );
		if ( *dest_top ) {
			FILE *f;
			path_cat( to, dest, "DIRS.LOG" );
			if ( ( f = fopen( to, "w" ) ) != NULL ) {
				fprintf( f, "%s\n", dest_top );
				fclose( f );
			}
		}
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
		if ( keep_file( to ) )
			continue;
		log_file( to );
		if ( copy_file( from, to ) ) {
			char msg[160];
			sprintf( msg, "%s %s -> %s", T( "CopyError", "Cannot copy" ), from, to );
			fail( msg );
		}
	}
	/* [Find] files from the installation disk that are used */
	for ( i = 0; i < nfind; i++ )
		if ( finds[i].file[0] && find_used( finds[i].name ) ) {
			path_cat( from, srcdir, finds[i].file );
			scr_progress( progtitle, finds[i].file, done, total );
			log_file( finds[i].path );
			if ( copy_file( from, finds[i].path ) ) {
				char msg[160];
				sprintf( msg, "%s %s -> %s", T( "CopyError", "Cannot copy" ), from, finds[i].path );
				fail( msg );
			}
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
	old_log_flush();
	if ( logf ) {
		fclose( logf );
		logf = NULL;
	}
}

/* [Run] Before= / After=: programs to run (cond|command line); a program
 * without a path is taken from the directory of INSTALL.EXE */
static int run_program( const char *cmdline )
{
	int rc;
	scr_exit();
	rc = spawn_program( cmdline );
	scr_init();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne Dual Installer" ), "" );
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
		path_cat( p, windir, "WIN.INI" );
		if ( !frontend() && file_exists( p ) )
			edit_file( p, "Win.ini", 3 );
	}
}

static void remove_all( void )
{
	const char *v, *t;
	char p[80], name[40];
	int pos = 0, had_log = 0, pending;
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
	patch_all();
	edit_all();
	progman_undo();
	pending = uninstall_registry();
	path_cat( p, dest, "INSTALL.LOG" );
	if ( ( logf = fopen( p, "r" ) ) != NULL ) {
		/* the files installed (from archives: also below %DIR%); a
		 * replaced file comes back from its backup */
		char l[180];
		had_log = 1;
		while ( fgets( l, sizeof(l), logf ) ) {
			char *e, *b;
			strip_eol( l );
			if ( ( e = strchr( l, '|' ) ) != NULL ) {
				*e++ = 0;
				if ( file_exists( e ) ) {
					remove( l );
					if ( rename( e, l ) && !copy_file( e, l ) )
						remove( e );
				}
				continue;
			}
			runner_path( p );
			if ( pending && !stricmp( l, p ) )
				continue;   /* runs at the next start of Windows */
			remove( l );
			while ( ( b = strrchr( l, '\\' ) ) != NULL && b - l > 2 && stricmp( l, dest ) ) {
				*b = 0;
				if ( rmdir( l ) )
					break;
			}
		}
		fclose( logf );
		logf = NULL;
		path_cat( p, dest, "INSTALL.LOG" );
		remove( p );
	}
	pos = 0;
	while ( !had_log && ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
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
	record_write( 0 );
	if ( !keep_uninstaller && !pending ) {
		path_cat( p, dest, "INSTALL.INI" );
		remove( p );
		path_cat( p, dest, "INSTALL.EXE" );
		remove( p );
		gdi_remove_dirs( dest );
	}
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
/* A single choice, shared by DOS setup and the Windows frontend's DOS
 * transaction. Package files, not separate executable builds, carry text. */
static void ask_language( void )
{
	const char *value;
	char codes[8][9], labels[8][32];
	const char *buttons[8];
	int pos = 0, count = 0, selected = 0, i;
	while ( ( pos = ini_next( "Languages", "Choice", pos, &value ) ) >= 0 ) {
		const char *bar = strchr( value, '|' );
		int length = bar ? (int)( bar - value ) : 0;
		if ( count == 8 || length < 2 || length > 8 || !bar[1] ||
			strlen( bar + 1 ) > 31 )
			fail( "Invalid [Languages] Choice entry." );
		for ( i = 0; i < length; i++ )
			if ( !isalnum( (unsigned char)value[i] ) && value[i] != '-' )
				fail( "Invalid language code." );
		sprintf( codes[count], "%.*s", length, value );
		strcpy( labels[count], bar + 1 );
		buttons[count] = labels[count];
		if ( !stricmp( codes[count], ini_get( "Languages", "Default", "en" ) ) )
			selected = count;
		count++;
	}
	if ( !count ) return;
	selected = scr_message( T( "LanguageTitle", "Language" ),
		T( "LanguageText", "Select the application language to install:" ),
		buttons, count, selected, 0 );
	if ( selected < 0 ) abort_setup();
	strcpy( selected_language, codes[selected] );
}

/* Config=package-relative-file|section|key.  The owned INI file is copied
 * before this setting is written and removed by the normal uninstall log. */
static void write_language( void )
{
	const char *value = ini_get( "Languages", "Config", NULL );
	char data[200], path[80], setting[150], section[40], key[80], line[256];
	char *bar, *name;
	int index, end, i, error;
	FILE *output;
	if ( !value ) return;
	if ( strlen( value ) >= sizeof(data) ) fail( "Invalid language config." );
	strcpy( data, value );
	bar = strchr( data, '|' );
	if ( !bar ) fail( "Invalid language config." );
	*bar++ = 0;
	name = data;
	if ( !*name || name[0] == '\\' || strchr( name, ':' ) || strstr( name, ".." ) ||
		strlen( dest ) + strlen( name ) + 2 >= sizeof(path) ||
		strlen( bar ) + strlen( selected_language ) + 2 >= sizeof(setting) )
		fail( "Language config must be an installed package-relative file." );
	path_cat( path, dest, name );
	sprintf( setting, "%s=%s", bar, selected_language );
	if ( system_set_split( setting, section, key, line ) || ed_load( path ) )
		fail( "Cannot read the installed language config." );
	index = ini_find_key( section, key, &end );
	if ( index == -2 ) fail( "Duplicate language config key." );
	if ( index >= 0 ) {
		free( ed[index] );
		ed[index] = str_dup( line );
	} else if ( end >= 0 )
		ed_insert( end, line );
	else {
		char header[44];
		sprintf( header, "[%s]", section );
		ed_insert( ned, header );
		ed_insert( ned, line );
	}
	/* This is the package's just-copied file, not an external system INI.
	 * Do not leave untracked .G01 files in its uninstall directory. */
	output = fopen( path, "w" );
	if ( !output ) fail( "Cannot write the installed language config." );
	error = 0;
	for ( i = 0; i < ned; i++ )
		if ( fprintf( output, "%s\n", ed[i] ) < 0 ) error = 1;
	if ( fclose( output ) || error ) fail( "Cannot write the installed language config." );
	ed_free();
}

static void batch_choices( void );

static void install_steps( void )
{
	char registry_error[130];
	if ( !in_batch )
		welcome();
	find_update();
	if ( !in_batch )   /* [Installer]: asked for all packages before */
		license();
	scr_desktop();
	if ( !update_mode ) {
		ask_dirs();
		scr_desktop();
		if ( in_batch )
			batch_choices();
		else {
			ask_options();
			scr_desktop();
			ask_language();
		}
		scr_desktop();
		if ( registry_write_plan( registry_error ) )
			fail( registry_error );
	}
	task_write_plan();   /* an update: only the new Program Manager items */
	if ( !in_batch && !confirm() )
		abort_setup();
	scr_desktop();
	run_all( "Before" );
	copy_all();
	if ( !update_mode ) {   /* an update keeps the system settings */
		write_language();
		patch_all();
	}
	edit_all();      /* an update: only the entries new in this version */
	progman_all();
	if ( !update_mode )
		deferred_registry();
	if ( !stricmp( ini_get( "Setup", "Uninstall", "yes" ), "yes" ) )
		record_write( 1 );
	run_all( "After" );
	run_windows();
}

/* the INI (already loaded) of srcdir dir becomes the current one */
static void use_ini( const char *dir )
{
	strcpy( srcdir, dir );
	strcpy( selected_language, "en" );
	nopt = 0;
	memset( opt_on, 0, sizeof(opt_on) );
	memset( opt_value, 0, sizeof(opt_value) );
	dest[0] = windir[0] = 0;
	dosmode = !stricmp( ini_get( "Setup", "Mode", "" ), "DOS" );
	if ( !dosmode )
		find_windows();
	find_files();
	detect_all();
	read_options();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne Dual Installer" ), "" );
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

/* --- combined setup: [Installer] Inst1= ... Inst99= ------------------------
 * Each entry is a directory with INSTALL.INI or an .INI file, relative to
 * INSTALL.EXE ("off|DIR": not selected at first).  The packages chosen are
 * installed one after the other with their own INI: no welcome, directory
 * or summary dialogs, but their licenses and options. */
#define MAXINST 99
static char *inst_path[MAXINST], *inst_label[MAXINST], *inst_choice[MAXINST];
static char inst_on[MAXINST], inst_state[MAXINST];
static int ninst, cur_inst;

/* the options and language chosen for package cur_inst: "0110|de" */
static void batch_choices( void )
{
	const char *c = inst_choice[cur_inst], *bar;
	int i;
	if ( !c )
		return;
	for ( i = 0; i < nopt && c[i] && c[i] != '|'; i++ )
		opt_on[i] = c[i] == '1';
	if ( ( bar = strchr( c, '|' ) ) != NULL && bar[1] )
		sprintf( selected_language, "%.8s", bar + 1 );
}

/* first pass: licenses, options and language of package cur_inst */
static void batch_ask( void )
{
	char c[MAXOPT + 12];
	int i;
	find_update();
	license();
	scr_desktop();
	if ( !update_mode ) {
		ask_options();
		scr_desktop();
		ask_language();
		scr_desktop();
	}
	for ( i = 0; i < nopt; i++ )
		c[i] = opt_on[i] ? '1' : '0';
	sprintf( c + nopt, "|%s", selected_language );
	inst_choice[cur_inst] = str_dup( c );
}

/* directory and INI file of an entry */
static void inst_files( const char *v, char *dir, char *ini )
{
	char *b;
	int n;
	if ( v[0] && v[1] == ':' )
		sprintf( dir, "%.79s", v );
	else
		path_cat( dir, srcdir, v );
	n = strlen( dir );
	if ( n > 4 && !stricmp( dir + n - 4, ".INI" ) ) {
		strcpy( ini, dir );
		if ( ( b = strrchr( dir, '\\' ) ) != NULL )
			*b = 0;
	} else
		path_cat( ini, dir, "INSTALL.INI" );
}

/* a [Setup] value of another INI (without loading it) */
static void ini_peek( const char *path, const char *key, char *out, int size )
{
	FILE *f = fopen( path, "r" );
	char l[200];
	int in = 0, n = strlen( key );
	*out = 0;
	if ( !f )
		return;
	while ( fgets( l, sizeof(l), f ) ) {
		strip_eol( l );
		if ( *l == '[' )
			in = !strnicmp( l, "[Setup]", 7 );
		else if ( in && !strnicmp( l, key, n ) && l[n] == '=' ) {
			sprintf( out, "%.*s", size - 1, l + n + 1 );
			break;
		}
	}
	fclose( f );
}

static void read_installers( void )
{
	char key[12], dir[80], ini[80], product[70], id[64], d2[80], bits[MAXOPT + 1], version[24], label[100];
	const char *v;
	int i, k;
	ninst = 0;
	for ( i = 1; i <= MAXINST && ninst < MAXINST; i++ ) {
		sprintf( key, "Inst%d", i );
		if ( !( v = ini_get( "Installer", key, NULL ) ) || !*v )
			continue;
		inst_on[ninst] = 1;
		if ( !strnicmp( v, "off|", 4 ) ) {
			inst_on[ninst] = 0;
			v += 4;
		} else if ( !strnicmp( v, "on|", 3 ) )
			v += 3;
		inst_files( v, dir, ini );
		ini_peek( ini, "Product", product, sizeof(product) );
		if ( !*product )
			sprintf( product, "%.69s", v );
		/* the key of GDI.INI, as ini_package_id() makes it */
		ini_peek( ini, "Id", id, sizeof(id) );
		if ( !*id )
			strcpy( id, product );
		for ( k = 0; id[k]; k++ )
			if ( id[k] == '=' || id[k] == '[' || id[k] == ';' )
				id[k] = '_';
		if ( !file_exists( ini ) ) {
			sprintf( label, "%s (%s)", product, T( "InstMissing", "missing" ) );
			inst_on[ninst] = 0;
		} else if ( record_read_key( id, d2, bits, version ) )
			sprintf( label, "%s (%s)", product, T( "InstInstalled", "installed" ) );
		else
			strcpy( label, product );
		inst_path[ninst] = str_dup( v );
		inst_label[ninst] = str_dup( label );
		inst_state[ninst] = 0;
		ninst++;
	}
}

static void run_installers( void )
{
	static char maindir[80], mainwin[80], plan[80], dir[80], ini[80];
	static char text[1500];
	static const char *ok[1];
	const char *saved_override = option_override;
	int i, rc, done = 0, pass;
	read_installers();
	if ( !ninst )
		return;
	scr_keys( T( "KeysOpt", "Space=On/off  Enter=Continue  Esc=Abort" ) );
	if ( stricmp( ini_get( "Installer", "Select", "yes" ), "no" ) &&
		scr_checklist( T( "InstTitle", "Packages" ),
		T( "InstText", "Select the packages to install (Space switches on/off):" ), (const char **)inst_label, inst_on, ninst ) )
		abort_setup();
	scr_desktop();
	find_windows();
	scr_keys( T( "KeysInput", "Enter=Continue  Esc=Abort" ) );
	if ( scr_input( T( "WinTitle", "Windows" ),
		T( "WinText", "Windows directory for the SYSTEM.INI entries (empty: no Windows):" ), windir, 64 ) )
		abort_setup();
	strcpy( maindir, srcdir );
	strcpy( mainwin, windir );
	strcpy( plan, registry_plan_file );
	option_override = NULL;
	/* first all questions (licenses, options), then the installations */
	for ( pass = 0; pass < 2; pass++ )
		for ( i = 0; i < ninst; i++ ) {
			if ( !inst_on[i] || inst_state[i] )
				continue;
			inst_files( inst_path[i], dir, ini );
			scr_desktop();
			ini_save();
			if ( ini_load( ini ) ) {
				ini_restore();
				inst_state[i] = 2;
				continue;
			}
			if ( !stricmp( ini_get( "Setup", "Windows", "yes" ), "no" ) && windows_running() ) {
				ini_restore();
				inst_state[i] = 4;   /* only from MS-DOS */
				continue;
			}
			use_ini( dir );
			strcpy( windir, mainwin );
			registry_plan_file[0] = 0;   /* the registry: a runner in each directory */
			registry_deferred = 0;
			in_extra = in_batch = 1;
			cur_inst = i;
			if ( ( rc = setjmp( extra_abort ) ) == 0 ) {
				if ( pass == 0 )
					batch_ask();
				else {
					install_steps();
					inst_state[i] = 1;
					done++;
				}
			} else
				inst_state[i] = rc == 1 ? 3 : 2;
			in_extra = in_batch = 0;
			ini_restore();
			use_ini( maindir );
			strcpy( windir, mainwin );
		}
	strcpy( registry_plan_file, plan );
	option_override = saved_override;
	sprintf( text, T( "InstDone", "%d packages are installed." ), done );
	for ( i = 0; i < ninst; i++ )
		if ( inst_on[i] && inst_state[i] != 1 && strlen( text ) + strlen( inst_label[i] ) + 60 < sizeof(text) )
			sprintf( text + strlen( text ), "\n%s: %s", inst_label[i],
				inst_state[i] == 3 ? T( "InstCancelled", "cancelled" ) :
				inst_state[i] == 4 ? T( "InstNeedDos", "only from MS-DOS" ) : T( "InstFailed", "failed" ) );
	ok[0] = T( "OK", "OK" );
	scr_desktop();
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	scr_message( T( "InstTitle", "Packages" ), text, ok, 1, 0, 0 );
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
	printf( "GeosOne Dual Installer " GDI_VERSION " - GNU GPL version 3\n"
	        "INSTALL [/U] [/B:d:] [/O:bits] [/W:status] [file.INI]\n"
	        "  installs as described by INSTALL.INI next to INSTALL.EXE\n"
	        "  /U    removes the installation (run INSTALL.EXE in the target directory)\n"
	        "  /B:d: drive of the system to change (CONFIG.SYS, AUTOEXEC.BAT),\n"
	        "        default: the boot drive\n"
	        "  /O:   initial option states, e.g. /O:1010 (used by Windows setup)\n"
	        "  /W:   write completion status for the Windows setup frontend\n"
	        "  /R:   write its expanded registry plan (internal)\n"
	        "  /F:   registry family: 31 or 9X (internal)\n"
	        "  /T:   write the WIN.INI and Program Manager plan (internal)\n"
	        "  /K    keep the uninstaller until native cleanup (internal)\n" );
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
			else if ( toupper( argv[i][1] ) == 'K' && !argv[i][2] )
				keep_uninstaller = 1;
			else if ( toupper( argv[i][1] ) == 'B' && argv[i][2] == ':' && isalpha( argv[i][3] ) ) {
				boot[0] = toupper( argv[i][3] );
				bootset = 1;
			} else if ( toupper( argv[i][1] ) == 'O' && argv[i][2] == ':' ) {
				option_override = argv[i] + 3;
			} else if ( toupper( argv[i][1] ) == 'W' && argv[i][2] == ':' && argv[i][3] ) {
				sprintf( completion_file, "%.79s", argv[i] + 3 );
				if ( !completion_registered ) {
					atexit( write_completion );
					completion_registered = 1;
				}
			} else if ( toupper( argv[i][1] ) == 'T' && argv[i][2] == ':' && argv[i][3] ) {
				sprintf( task_plan_file, "%.79s", argv[i] + 3 );
			} else if ( toupper( argv[i][1] ) == 'R' && argv[i][2] == ':' && argv[i][3] ) {
				sprintf( registry_plan_file, "%.79s", argv[i] + 3 );
			} else if ( toupper( argv[i][1] ) == 'F' && argv[i][2] == ':' ) {
				if ( !stricmp( argv[i] + 3, "31" ) )
					registry_family = 3;
				else if ( !stricmp( argv[i] + 3, "9X" ) )
					registry_family = 9;
				else {
					usage();
					return 2;
				}
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
	if ( *registry_plan_file ) {
		char registry_error[130];
		if ( !registry_family ) {
			printf( "INSTALL: /R requires /F:31 or /F:9X\n" );
			return 2;
		}
		if ( registry_preflight( registry_error ) ) {
			printf( "INSTALL: %s\n", registry_error );
			return 2;
		}
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
	detect_all();

	scr_init();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne Dual Installer" ), "" );
	scr_desktop();
	read_options();
	if ( !stricmp( ini_get( "Setup", "Windows", "yes" ), "no" ) && windows_running() )
		fail( T( "NeedDos", "Please quit Windows and run this setup from MS-DOS." ) );
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
		} else if ( ini_get( "Installer", "Inst1", NULL ) ) {
			welcome();
			license();
			scr_desktop();
			run_installers();
		} else
			install_steps();
		if ( !stricmp( ini_get( "Setup", "Extras", "no" ), "yes" ) )
			extras();
	}
	scr_desktop();
	finish();
	completion_status = 0;
	scr_exit();
	return 0;
}
