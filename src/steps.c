/* steps.c - GeosOne Dual Installer, the setup steps: welcome, license,
 * options, directories, [Find], [Detect]
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

/* --- setup steps ------------------------------------------------------ */

void find_windows( void )
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

void find_files( void )
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

/* [Detect] NAME=program arguments: NAME becomes true when the external
 * program returns zero.  The installer has no knowledge of what it tests. */
void detect_all( void )
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
void find_dest( void )
{
	int i;
	for ( i = 0; i < nfind; i++ )
		if ( finds[i].file[0] )
			path_cat( finds[i].path, dest, finds[i].file );
}

void show_text( const char *title, const char *file, const char **buttons, int n, int must )
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
	rc = ui_view( title, l, nl, buttons, n, 0 );
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
	ui_clear();
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

void license( void )
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

void welcome( void )
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
	if ( ui_message( T( "WelcomeTitle", "Welcome" ), text, b, 2, 0, 0 ) != 0 )
		abort_setup();
}

void read_options( void )
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

void ask_options( void )
{
	char title[90];
	if ( !nopt )
		return;
	sprintf( title, "%.40s", T( "OptionsTitle", "Options" ) );
	if ( in_batch )   /* [Installer]: which package */
		sprintf( title + strlen( title ), " - %.45s", ini_get( "Setup", "Product", "" ) );
	if ( ui_checklist( title,
		T( "OptionsText", "Select the parts to install (Space switches on/off):" ), opt_label, opt_on, nopt ) )
		abort_setup();
}

void ask_dirs( void )
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
	for ( ;; ) {
		if ( ui_input( T( "DirTitle", "Destination" ), T( "DirText", "Install the files to this directory:" ), dest, 64 ) )
			abort_setup();
		gdi_note_parents( dest, dest_top );
		if ( strlen( dest ) >= 3 && dest[1] == ':' && dest[2] == '\\' && !mkdirs( dest ) )
			break;
		{
			static const char *ok[] = { "OK" };
			ui_message( T( "ErrorTitle", "Error" ), T( "DirError", "Please enter a full path (e.g. C:\\DRIVERS) that can be created." ), ok, 1, 0, 1 );
		}
	}
	find_dest();
	if ( has_entry( "System.ini", "Add" ) || has_entry( "System.ini", "Set" ) ||
		has_entry( "Win.ini", "Set" ) || has_progman() ||
		ini_lines( "Registry", 0, &value ) >= 0 || hexpatch_needs_windows() ) {
		find_windows();
		if ( ui_input( T( "WinTitle", "Windows" ),
			T( "WinText", "Windows directory for the SYSTEM.INI entries (empty: no Windows):" ), windir, 64 ) )
			abort_setup();
	}
}

int confirm( void )
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
				strlen( text ) + strlen( group ) + strlen( title ) + 40 < sizeof(text) ) {
				gdi_pm_ansi_to_oem( group );   /* shown as a text of the INI */
				gdi_pm_ansi_to_oem( title );
				sprintf( text + strlen( text ), "%s %s: %s\n",
					T( "SumProgMan", "Program Manager" ), group, title );
			}
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
	return ui_message( update_mode ? T( "SumUpdateTitle", "Ready to update" ) :
		T( "SumTitle", "Ready to install" ), text, b, 2, 0, 0 ) == 0;
}

/* is %NAME% used in an active line of the configuration sections? */
int find_used( const char *name )
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
