/* wintasks.c - WIN.INI values and Program Manager items for the Win16
 * frontend.  The DOS engine resolves the package's [Win.ini] and [ProgMan]
 * entries into a plan (/T:); this module applies it while Windows runs:
 * WIN.INI through the profile functions (Windows keeps WIN.INI in memory),
 * the groups through DDE with Program Manager.  It writes the same journals
 * as the DOS engine in plain DOS (WIN.SET, PROGMAN.LOG), so INSTALL /U works
 * from either side.
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <windows.h>
#include <ddeml.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wintasks.h"

#define ABSENT "<GDI-ABSENT>"   /* GetProfileString default: no such value */

static DWORD dde_instance;
static HCONV conversation;

static void set_error( char *error, unsigned size, const char *text )
{
	if ( size ) {
		strncpy( error, text, size - 1 );
		error[size - 1] = 0;
	}
}

static void path_cat( char *out, const char *dir, const char *name )
{
	strcpy( out, dir );
	if ( *out && out[strlen( out ) - 1] != '\\' )
		strcat( out, "\\" );
	strcat( out, name );
}

static void strip_eol( char *line )
{
	int n = strlen( line );
	while ( n && ( line[n - 1] == '\n' || line[n - 1] == '\r' ) )
		line[--n] = 0;
}

/* --- WIN.INI ------------------------------------------------------------ */

/* is section/key already in the journal (an earlier installation)? */
static int journal_has( const char *journal, const char *section, const char *key )
{
	FILE *f = fopen( journal, "r" );
	char s[40], k[80], flag[8], old[256], new_line[256];
	int found = 0;
	if ( !f )
		return 0;
	fgets( s, sizeof(s), f );   /* header */
	while ( !found && fgets( s, sizeof(s), f ) && fgets( k, sizeof(k), f ) &&
	        fgets( flag, sizeof(flag), f ) && fgets( old, sizeof(old), f ) &&
	        fgets( new_line, sizeof(new_line), f ) ) {
		strip_eol( s );
		strip_eol( k );
		found = !stricmp( s, section ) && !stricmp( k, key );
	}
	fclose( f );
	return found;
}

/* W|section|key=value */
static int apply_value( const char *dir, char *line, char *error, unsigned size )
{
	char journal[160], old[256], *section, *key, *value, *bar, *eq;
	FILE *f;
	int exists;
	section = line + 2;
	if ( !( bar = strchr( section, '|' ) ) || !( eq = strchr( bar + 1, '=' ) ) ) {
		set_error( error, size, "Invalid WIN.INI entry in the plan" );
		return 1;
	}
	*bar = 0;
	key = bar + 1;
	*eq = 0;
	value = eq + 1;
	path_cat( journal, dir, "WIN.SET" );
	if ( !journal_has( journal, section, key ) ) {
		exists = 1;
		{
			FILE *test = fopen( journal, "r" );
			if ( test )
				fclose( test );
			else
				exists = 0;
		}
		GetProfileString( section, key, ABSENT, old, sizeof(old) );
		if ( !( f = fopen( journal, "a" ) ) ) {
			set_error( error, size, "Cannot write WIN.SET" );
			return 1;
		}
		if ( !exists )
			fprintf( f, "GDI-SYSTEM-SET 1\n" );
		if ( strcmp( old, ABSENT ) )
			fprintf( f, "%s\n%s\n1\n%s=%s\n%s=%s\n", section, key, key, old, key, value );
		else
			fprintf( f, "%s\n%s\n0\n\n%s=%s\n", section, key, key, value );
		if ( fclose( f ) ) {
			set_error( error, size, "Cannot write WIN.SET" );
			return 1;
		}
	}
	if ( !WriteProfileString( section, key, value ) ) {
		set_error( error, size, "Cannot change WIN.INI" );
		return 1;
	}
	return 0;
}

static int restore_values( const char *dir, char *error, unsigned size )
{
	char journal[160], s[40], k[80], flag[8], old[256], new_line[256], now[256];
	FILE *f;
	path_cat( journal, dir, "WIN.SET" );
	if ( !( f = fopen( journal, "r" ) ) )
		return 0;
	if ( !fgets( s, sizeof(s), f ) || strncmp( s, "GDI-SYSTEM-SET 1", 16 ) ) {
		fclose( f );
		set_error( error, size, "Invalid WIN.SET" );
		return 1;
	}
	while ( fgets( s, sizeof(s), f ) && fgets( k, sizeof(k), f ) &&
	        fgets( flag, sizeof(flag), f ) && fgets( old, sizeof(old), f ) &&
	        fgets( new_line, sizeof(new_line), f ) ) {
		char *v;
		strip_eol( s ); strip_eol( k ); strip_eol( flag );
		strip_eol( old ); strip_eol( new_line );
		/* keep a value that was changed after the installation */
		GetProfileString( s, k, ABSENT, now, sizeof(now) );
		v = strchr( new_line, '=' );
		if ( !v || strcmp( now, v + 1 ) )
			continue;
		if ( flag[0] == '1' && ( v = strchr( old, '=' ) ) != NULL )
			WriteProfileString( s, k, v + 1 );
		else {
			WriteProfileString( s, k, NULL );
			/* a section without keys left was created by Set= */
			if ( !GetProfileString( s, NULL, "", now, sizeof(now) ) )
				WriteProfileString( s, NULL, NULL );
		}
	}
	fclose( f );
	remove( journal );
	return 0;
}

/* a program of [Run] Windows=; a program that does not start does not undo
 * the installation, it is only reported */
void gdi_tasks_start( const char *command )
{
	char text[200];
	if ( WinExec( command, SW_SHOWNORMAL ) < 32 ) {
		sprintf( text, "Cannot start %.160s", command );
		MessageBox( NULL, text, "GDI", MB_OK | MB_ICONEXCLAMATION );
	}
}

/* --- Program Manager (DDE) --------------------------------------------- */

static HDDEDATA CALLBACK dde_callback( UINT type, UINT format, HCONV conv,
	HSZ hsz1, HSZ hsz2, HDDEDATA data, DWORD data1, DWORD data2 )
{
	(void)type; (void)format; (void)conv; (void)hsz1; (void)hsz2;
	(void)data; (void)data1; (void)data2;
	return NULL;
}

static FARPROC callback_thunk;

static int pm_connect( void )
{
	HSZ name;
	if ( conversation )
		return 0;
	callback_thunk = MakeProcInstance( (FARPROC)dde_callback, GetModuleHandle( NULL ) );
	if ( DdeInitialize( &dde_instance, (PFNCALLBACK)callback_thunk,
	                    APPCMD_CLIENTONLY, 0 ) != DMLERR_NO_ERROR )
		return 1;
	name = DdeCreateStringHandle( dde_instance, "PROGMAN", CP_WINANSI );
	conversation = DdeConnect( dde_instance, name, name, NULL );
	DdeFreeStringHandle( dde_instance, name );
	return conversation ? 0 : 1;
}

static void pm_disconnect( void )
{
	if ( conversation )
		DdeDisconnect( conversation );
	conversation = NULL;
	if ( dde_instance )
		DdeUninitialize( dde_instance );
	dde_instance = 0;
}

static int pm_execute( const char *command )
{
	return !DdeClientTransaction( (LPBYTE)command, strlen( command ) + 1,
	                              conversation, NULL, 0, XTYP_EXECUTE, 30000, NULL );
}

/* the text Program Manager returns for an item (group list or a group) */
static char *pm_request( const char *item )
{
	HSZ name = DdeCreateStringHandle( dde_instance, item, CP_WINANSI );
	HDDEDATA data = DdeClientTransaction( NULL, 0, conversation, name, CF_TEXT,
	                                      XTYP_REQUEST, 30000, NULL );
	char *text = NULL;
	DWORD n;
	DdeFreeStringHandle( dde_instance, name );
	if ( !data )
		return NULL;
	n = DdeGetData( data, NULL, 0, 0 );
	if ( n < 60000L && ( text = (char *)malloc( (size_t)n + 1 ) ) != NULL ) {
		DdeGetData( data, (LPBYTE)text, n, 0 );
		text[n] = 0;
	}
	DdeFreeDataHandle( data );
	return text;
}

/* the group exists (a line of the group list) */
static int pm_group_exists( const char *group )
{
	char *list = pm_request( "PROGMAN" ), *line;
	int found = 0;
	if ( !list )
		return 0;
	for ( line = strtok( list, "\r\n" ); line && !found; line = strtok( NULL, "\r\n" ) )
		found = !stricmp( line, group );
	free( list );
	return found;
}

/* number of items of the group; *has = an item with this title */
static int pm_items( const char *group, const char *title, int *has )
{
	char *text = pm_request( group ), *line, quoted[90];
	int count = -1;
	*has = 0;
	if ( !text )
		return -1;
	if ( title )
		sprintf( quoted, "\"%.80s\",", title );
	for ( line = strtok( text, "\r\n" ); line; line = strtok( NULL, "\r\n" ) ) {
		count++;   /* the first line describes the group */
		if ( count && title && !strnicmp( line, quoted, strlen( quoted ) ) )
			*has = 1;
	}
	free( text );
	return count;
}

/* P|group|title|command|icon|index */
static int apply_item( FILE *journal_file, char *line, char *error, unsigned size )
{
	char *field[5], *p, command[400];
	int n = 0, has;
	for ( p = line + 2, field[n++] = p; *p && n < 5; p++ )
		if ( *p == '|' ) {
			*p = 0;
			field[n++] = p + 1;
		}
	if ( n < 5 ) {
		set_error( error, size, "Invalid Program Manager entry in the plan" );
		return 1;
	}
	if ( ( p = strchr( field[4], '|' ) ) != NULL )
		*p++ = 0;
	if ( pm_group_exists( field[0] ) ) {
		if ( pm_items( field[0], field[1], &has ) >= 0 && has )
			return 0;   /* already there */
	} else
		fprintf( journal_file, "G|%s\n", field[0] );
	sprintf( command, "[CreateGroup(\"%s\")]", field[0] );
	if ( pm_execute( command ) ) {
		set_error( error, size, "Program Manager cannot create the group" );
		return 1;
	}
	sprintf( command, "[AddItem(\"%s\",\"%s\",\"%s\",%d)]", field[2], field[1],
	         field[3], p ? atoi( p ) : 0 );
	if ( pm_execute( command ) ) {
		set_error( error, size, "Program Manager cannot add the item" );
		return 1;
	}
	fprintf( journal_file, "I|%s|%s\n", field[0], field[1] );
	return 0;
}

static int restore_items( const char *dir, char *error, unsigned size )
{
	char journal[160], l[200], command[200], *bar;
	FILE *f;
	int pass, has;
	path_cat( journal, dir, "PROGMAN.LOG" );
	if ( !( f = fopen( journal, "r" ) ) )
		return 0;
	fclose( f );
	if ( pm_connect() ) {
		set_error( error, size, "Program Manager does not answer (DDE)" );
		return 1;
	}
	for ( pass = 0; pass < 2; pass++ ) {   /* items, then empty groups */
		if ( !( f = fopen( journal, "r" ) ) )
			break;
		while ( fgets( l, sizeof(l), f ) ) {
			strip_eol( l );
			if ( pass == 0 && l[0] == 'I' && l[1] == '|' && ( bar = strchr( l + 2, '|' ) ) != NULL ) {
				*bar = 0;
				if ( !pm_group_exists( l + 2 ) || pm_items( l + 2, bar + 1, &has ) < 0 || !has )
					continue;
				/* CreateGroup activates an existing group; DeleteItem
				 * works on the active one */
				sprintf( command, "[CreateGroup(\"%s\")][DeleteItem(\"%s\")]", l + 2, bar + 1 );
				pm_execute( command );
			}
			if ( pass == 1 && l[0] == 'G' && l[1] == '|' && pm_group_exists( l + 2 ) &&
			     pm_items( l + 2, NULL, &has ) == 0 ) {
				sprintf( command, "[DeleteGroup(\"%s\")]", l + 2 );
				pm_execute( command );
			}
		}
		fclose( f );
	}
	pm_disconnect();
	remove( journal );
	return 0;
}

/* --- plan ------------------------------------------------------------------ */

int gdi_tasks_apply( const char *plan_path, char *error, unsigned size )
{
	char line[600], dir[128], journal[160];
	FILE *plan, *items = NULL;
	int failed = 0;
	if ( !( plan = fopen( plan_path, "r" ) ) )
		return 0;   /* nothing to do */
	if ( !fgets( line, sizeof(line), plan ) || strncmp( line, "GDI-TASK-PLAN 1", 15 ) ||
	     !fgets( line, sizeof(line), plan ) || strncmp( line, "Dir=", 4 ) ) {
		fclose( plan );
		set_error( error, size, "Invalid plan for Windows" );
		return 1;
	}
	strip_eol( line );
	sprintf( dir, "%.127s", line + 4 );
	while ( !failed && fgets( line, sizeof(line), plan ) ) {
		strip_eol( line );
		if ( !strncmp( line, "Dir=", 4 ) ) {
			/* the next package of a combined setup: its own journals */
			if ( items && fclose( items ) ) {
				set_error( error, size, "Cannot write PROGMAN.LOG" );
				failed = 1;
			}
			items = NULL;
			sprintf( dir, "%.127s", line + 4 );
		} else if ( ( line[0] == 'R' || line[0] == 'U' ) && line[1] == '|' )
			gdi_tasks_run( line + 2, line[0] == 'R' );   /* the runner */
		else if ( line[0] == 'X' && line[1] == '|' )
			gdi_tasks_start( line + 2 );   /* [Run] Windows= */
		else if ( line[0] == 'W' && line[1] == '|' )
			failed = apply_value( dir, line, error, size );
		else if ( line[0] == 'P' && line[1] == '|' ) {
			if ( !items ) {
				path_cat( journal, dir, "PROGMAN.LOG" );
				if ( pm_connect() ) {
					set_error( error, size, "Program Manager does not answer (DDE)" );
					failed = 1;
					break;
				}
				if ( !( items = fopen( journal, "a" ) ) ) {
					set_error( error, size, "Cannot write PROGMAN.LOG" );
					failed = 1;
					break;
				}
			}
			failed = apply_item( items, line, error, size );
		}
	}
	fclose( plan );
	if ( items && fclose( items ) && !failed ) {
		set_error( error, size, "Cannot write PROGMAN.LOG" );
		failed = 1;
	}
	pm_disconnect();
	return failed;
}

int gdi_tasks_restore( const char *dir, char *error, unsigned size )
{
	return restore_values( dir, error, size ) || restore_items( dir, error, size );
}

/* add or remove a program in run= of [windows] */
void gdi_tasks_run( const char *program, int add )
{
	char value[256], out[256], *word;
	int found = 0;
	GetProfileString( "windows", "run", "", value, sizeof(value) );
	out[0] = 0;
	for ( word = strtok( value, " \t," ); word; word = strtok( NULL, " \t," ) ) {
		if ( !stricmp( word, program ) ) {
			found = 1;
			if ( !add )
				continue;
		}
		if ( strlen( out ) + strlen( word ) + 2 < sizeof(out) ) {
			if ( *out )
				strcat( out, " " );
			strcat( out, word );
		}
	}
	if ( found == add )
		return;
	if ( add && strlen( out ) + strlen( program ) + 2 < sizeof(out) ) {
		if ( *out )
			strcat( out, " " );
		strcat( out, program );
	}
	WriteProfileString( "windows", "run", out );
}
