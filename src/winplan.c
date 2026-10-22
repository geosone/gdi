/* winplan.c - GeosOne Dual Installer, Windows: WIN.INI run=, Program
 * Manager, registry, the plans for the frontend
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

/* --- Windows tasks: WIN.INI run=, Program Manager, the frontend's plan --- */

/* the engine was started by the Win16 frontend: WIN.INI and the groups are
 * changed by the frontend (Windows keeps them in memory while it runs) */
int frontend( void )
{
	return *completion_file != 0;
}

/* Windows runs (enhanced mode, or standard / real mode of Windows 3.x) */
int windows_running( void )
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
int progman_item( const char *value, char *group, char *title,
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

int has_progman( void )
{
	return has_entry( "ProgMan", "Item" );
}

/* plain DOS: the groups are changed in their files; PROGMAN.LOG records
 * "G|group" (created) and "I|group|title" (added) for INSTALL /U */
void progman_all( void )
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
void progman_undo( void )
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

void task_write_plan( void )
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
void runner_path( char *p )
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

void deferred_registry( void )
{
	if ( registry_deferred )
		add_runner();
}

/* [Run] Windows=cond|program: started once under Windows after setup, by
 * the frontend right away, from plain DOS by the runner at the next start
 * of Windows (RUNONCE.PLN) */
void run_windows( void )
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
int uninstall_registry( void )
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
