/* core.c - GeosOne Dual Installer, the core: texts, conditions, %DIR% and
 * the other variables, paths
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

char srcdir[80], dest[80], windir[80], boot[3] = "C:";
char opt_on[MAXOPT];
const char *opt_label[MAXOPT];
char opt_value[MAXOPT][24];   /* %On%: value when option n is on */
int nopt;
int uninstall, keep_uninstaller, dosmode, bootset;
const char *option_override;     /* /O:0101 from the Win16 frontend */
struct gdi_find finds[MAXFIND];   /* [Find] */
int nfind;
struct gdi_detect detects[MAXDETECT];   /* [Detect]: external program returned zero */
int ndetect;
FILE *logf;
int update_mode;          /* an earlier installation is updated */
char old_version[24];
const char *gdi_self;           /* argv[0] */
char completion_file[80];       /* /W:path: Win16 completion status */
char registry_plan_file[80];    /* /R:path: expanded native plan */
int registry_family;            /* /F:31 or /F:9X */
char task_plan_file[80];        /* /T:path: WIN.INI and groups for the frontend */
int registry_deferred;          /* REGISTRY.PLN for the runner (GDIPxxxx.EXE) */
int completion_status = 2;
int completion_registered;
jmp_buf extra_abort;            /* an extra installation was cancelled */
int in_batch;                   /* a package of [Installer] */
int in_extra;                   /* INSTALL.LOG: installed files */
char selected_language[9] = "en";

/* text of the installer, replaced by [Text] key=...; "\n" = new line */
const char *T( const char *key, const char *def )
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

const char *gdi_text( const char *key, const char *def )
{
	return T( key, def );
}

void write_completion( void )
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
int expand( char *out, const char *in, int max )
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
int named_result( const char *name, int n )
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
const char *cond( const char *v )
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

int condition_true( const char *condition )
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
int registry_preflight( char *message )
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
int registry_write_plan( char *message )
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

int contains( const char *s, const char *pat )
{
	int l = strlen( pat );
	for ( ; *s; s++ )
		if ( !strnicmp( s, pat, l ) )
			return 1;
	return 0;
}

int mkdirs( const char *path )
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
char dest_top[80];

int copy_file( const char *from, const char *to )
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

void fail( const char *text )
{
	static const char *ok[] = { "OK" };
	ui_message( T( "ErrorTitle", "Error" ), text, ok, 1, 0, 1 );
	if ( in_extra )
		longjmp( extra_abort, 2 );
	completion_status = 2;
	ui_exit();
	exit( 2 );
}

void gdi_out_of_memory( void )
{
	fail( T( "NoMemory", "Not enough memory." ) );
}

void abort_setup( void )
{
	static const char *ok[] = { "OK" };
	ui_message( T( "AbortTitle", "Setup" ), T( "Aborted", "Setup was cancelled. Nothing was changed." ), ok, 1, 0, 0 );
	if ( in_extra )
		longjmp( extra_abort, 1 );
	completion_status = 1;
	ui_exit();
	exit( 1 );
}
