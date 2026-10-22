/* combined.c - GeosOne Dual Installer, combined setup: [Installer] Inst1=
 * ... Inst99=
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

/* --- combined setup: [Installer] Inst1= ... Inst99= ------------------------
 * Each entry is a directory with INSTALL.INI or an .INI file, relative to
 * INSTALL.EXE ("off|DIR": not selected at first).  The packages chosen are
 * installed one after the other with their own INI: no welcome, directory
 * or summary dialogs, but their licenses and options. */
#define MAXINST 99
static char *inst_path[MAXINST], *inst_label[MAXINST], *inst_choice[MAXINST];
static char *inst_id[MAXINST], *inst_req[MAXINST], *inst_conf[MAXINST];
static char inst_on[MAXINST], inst_state[MAXINST], inst_ok[MAXINST];
static char inst_drive[MAXINST];          /* of DefaultDir= */
static unsigned long inst_need[MAXINST];  /* KB, package_space() */
static int ninst, cur_inst;
static char maindir[80], mainwin[80];

/* --- [Setup] Requires= and Conflicts=: Ids of other packages -------------
 * An Id is the key of GDI.INI ([Setup] Id=, else Product=).  A combined
 * setup selects the packages a selected one requires and installs them
 * first; a single package asks before it goes on without them. */

/* the next Id of a comma list, as ini_package_id() makes it; 0: none left */
static int next_id( const char **list, char *id, int size )
{
	const char *s = *list;
	int n = 0;
	while ( *s == ' ' || *s == ',' )
		s++;
	if ( !*s )
		return 0;
	for ( ; *s && *s != ','; s++ )
		if ( n + 1 < size )
			id[n++] = *s == '=' || *s == '[' || *s == ';' ? '_' : *s;
	while ( n && id[n - 1] == ' ' )
		n--;
	id[n] = 0;
	*list = s;
	return 1;
}

static int installed( const char *id )
{
	char dir[80], bits[MAXOPT + 1], version[24];
	return record_read_key( id, dir, bits, version );
}

/* a single package: a required package is missing or a conflicting one is
 * installed; go on anyway or cancel */
void check_requires( void )
{
	static const char *b[2];
	static char text[300];
	char id[64];
	const char *p;
	int conflict;
	for ( conflict = 0; conflict < 2; conflict++ ) {
		p = ini_get( "Setup", conflict ? "Conflicts" : "Requires", "" );
		while ( next_id( &p, id, sizeof(id) ) ) {
			if ( !installed( id ) == conflict )
				continue;
			if ( conflict )
				sprintf( text, T( "ConflictsText" ), id );
			else
				sprintf( text, T( "RequiresText" ), id );
			b[0] = T( "Continue" );
			b[1] = T( "Cancel" );
			ui_clear();
			if ( ui_message( T( "InstTitle" ), text, b, 2, 1, 0 ) ) {
				ui_clear();
				abort_setup();
			}
		}
	}
}

static int find_entry( const char *id )
{
	int j;
	for ( j = 0; j < ninst; j++ )
		if ( !stricmp( inst_id[j], id ) )
			return j;
	return -1;
}

static void add_line( char *text, int size, const char *a, const char *b )
{
	if ( strlen( text ) + strlen( a ) + strlen( b ) + 4 < (unsigned)size )
		sprintf( text + strlen( text ), b[0] ? "\n%s - %s" : "\n%s", a, b );
}

/* select the packages the selected ones require (added), unselect those
 * whose requirement is neither here nor installed (missing), list the
 * selected packages that conflict (conflicts) */
static void resolve( char *added, char *missing, char *conflicts, int size )
{
	char id[64];
	const char *p;
	int i, j, changed;
	*added = *missing = *conflicts = 0;
	do {
		changed = 0;
		for ( i = 0; i < ninst; i++ ) {
			if ( !inst_on[i] || !inst_req[i] )
				continue;
			p = inst_req[i];
			while ( next_id( &p, id, sizeof(id) ) ) {
				j = find_entry( id );
				if ( j >= 0 && inst_ok[j] ) {
					if ( !inst_on[j] && !installed( id ) ) {
						inst_on[j] = 1;
						add_line( added, size, inst_label[j], "" );
						changed = 1;
					}
				} else if ( !installed( id ) ) {
					inst_on[i] = 0;
					add_line( missing, size, inst_label[i], id );
					changed = 1;
					break;
				}
			}
		}
	} while ( changed );
	for ( i = 0; i < ninst; i++ ) {
		if ( !inst_on[i] || !inst_conf[i] )
			continue;
		p = inst_conf[i];
		while ( next_id( &p, id, sizeof(id) ) )
			if ( ( j = find_entry( id ) ) >= 0 && inst_on[j] )
				add_line( conflicts, size, inst_label[i], inst_label[j] );
			else if ( installed( id ) )
				add_line( conflicts, size, inst_label[i], id );
	}
}

/* 1: the packages package i requires are installed, 0: not yet, -1: one
 * of them failed */
static int requires_done( int i )
{
	char id[64];
	const char *p = inst_req[i];
	int j;
	if ( !p )
		return 1;
	while ( next_id( &p, id, sizeof(id) ) ) {
		j = find_entry( id );
		if ( j < 0 || j == i || !inst_on[j] )
			continue;
		if ( !inst_state[j] )
			return 0;
		if ( inst_state[j] != 1 )
			return -1;
	}
	return 1;
}

/* the package list: until the choice is consistent */
static void choose_installers( void )
{
	/* the texts on the heap: the data segment of the DOS engine is full */
	char *added = malloc( 3 * 500 + 1600 ), *missing, *conflicts, *text;
	static const char *b[2];
	int select = stricmp( ini_get( "Installer", "Select", "yes" ), "no" ) != 0;
	if ( !added )
		gdi_out_of_memory();
	missing = added + 500;
	conflicts = missing + 500;
	text = conflicts + 500;
	for ( ;; ) {
		if ( select && ui_checklist( T( "InstTitle" ),
			T( "InstText" ), (const char **)inst_label, inst_on, ninst ) )
			abort_setup();
		ui_clear();
		resolve( added, missing, conflicts, 500 );
		if ( *conflicts ) {
			sprintf( text, "%s%s", T( "ConflictText" ), conflicts );
			if ( !select )
				fail( text );
			b[0] = T( "OK" );
			ui_message( T( "InstTitle" ), text, b, 1, 0, 1 );
			ui_clear();
			continue;
		}
		if ( !*added && !*missing ) {
			free( added );
			return;
		}
		*text = 0;
		if ( *added )
			sprintf( text, "%s%s", T( "NeedsAdded" ), added );
		if ( *missing )
			sprintf( text + strlen( text ), "%s%s%s", *text ? "\n\n" : "", T( "NeedsMissing" ), missing );
		b[0] = T( "Continue" );
		b[1] = T( "Back" );
		if ( !ui_message( T( "InstTitle" ), text, b, select ? 2 : 1, 0, 0 ) ) {
			free( added );
			return;
		}
		ui_clear();
	}
}

/* the options and language chosen for package cur_inst: "0110|de" */
void batch_choices( void )
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
	char c[MAXOPT + 12], d[80];
	int i;
	check_versions();
	expand( d, ini_get( "Setup", "DefaultDir", "C:\\PROGRAM" ), sizeof(d) );
	inst_drive[cur_inst] = d[0];
	inst_need[cur_inst] = package_space();
	find_update();
	license();
	ui_clear();
	if ( !update_mode ) {
		ask_options();
		ui_clear();
		ask_language();
		ui_clear();
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
	char list[200];
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
		inst_id[ninst] = str_dup( id );
		ini_peek( ini, "Requires", list, sizeof(list) );
		inst_req[ninst] = *list ? str_dup( list ) : NULL;
		ini_peek( ini, "Conflicts", list, sizeof(list) );
		inst_conf[ninst] = *list ? str_dup( list ) : NULL;
		inst_ok[ninst] = file_exists( ini );
		if ( !inst_ok[ninst] ) {
			sprintf( label, "%s (%s)", product, T( "InstMissing" ) );
			inst_on[ninst] = 0;
		} else if ( record_read_key( id, d2, bits, version ) )
			sprintf( label, "%s (%s)", product, T( "InstInstalled" ) );
		else
			strcpy( label, product );
		inst_path[ninst] = str_dup( v );
		inst_label[ninst] = str_dup( label );
		inst_state[ninst] = 0;
		ninst++;
	}
}

/* pass 0: the questions of package i, pass 1: its installation */
static int run_one( int i, int pass )
{
	static char dir[80], ini[80];
	int rc, done = 0;
	inst_files( inst_path[i], dir, ini );
	ui_clear();
	ini_save();
	if ( ini_load( ini ) ) {
		ini_restore();
		inst_state[i] = 2;
		return 0;
	}
	if ( !stricmp( ini_get( "Setup", "Windows", "yes" ), "no" ) && windows_running() ) {
		ini_restore();
		inst_state[i] = 4;   /* only from MS-DOS */
		return 0;
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
			done = 1;
		}
	} else
		inst_state[i] = rc == 1 ? 3 : 2;
	in_extra = in_batch = 0;
	ini_restore();
	use_ini( maindir );
	strcpy( windir, mainwin );
	return done;
}

void run_installers( void )
{
	static char plan[80];
	static char text[1500];
	static const char *ok[1];
	const char *saved_override = option_override;
	int i, r, done = 0, waiting, progress, force = 0;
	read_installers();
	if ( !ninst )
		return;
	choose_installers();
	find_windows();
	if ( ui_input( T( "WinTitle" ),
		T( "WinText" ), windir, 64 ) )
		abort_setup();
	strcpy( maindir, srcdir );
	strcpy( mainwin, windir );
	strcpy( plan, registry_plan_file );
	option_override = NULL;
	/* first all questions (licenses, options), then the installations, a
	 * required package before the ones that need it */
	for ( i = 0; i < ninst; i++ )
		if ( inst_on[i] && !inst_state[i] )
			run_one( i, 0 );
	/* the room for all chosen packages, per drive */
	for ( i = 0; i < ninst; i++ ) {
		unsigned long need = 0;
		int j;
		if ( !inst_on[i] || inst_state[i] )
			continue;
		for ( j = 0; j < i && !( inst_on[j] && !inst_state[j] &&
			toupper( (unsigned char)inst_drive[j] ) == toupper( (unsigned char)inst_drive[i] ) ); j++ )
			;
		if ( j < i )
			continue;   /* that drive is counted */
		for ( j = i; j < ninst; j++ )
			if ( inst_on[j] && !inst_state[j] &&
				toupper( (unsigned char)inst_drive[j] ) == toupper( (unsigned char)inst_drive[i] ) )
				need += inst_need[j];
		check_space( inst_drive[i], need );
	}
	do {
		waiting = progress = 0;
		for ( i = 0; i < ninst; i++ ) {
			if ( !inst_on[i] || inst_state[i] )
				continue;
			if ( ( r = requires_done( i ) ) < 0 ) {
				inst_state[i] = 5;   /* a required package failed */
				progress = 1;
			} else if ( r || force ) {
				done += run_one( i, 1 );
				progress = 1;
			} else
				waiting = 1;
		}
		force = !progress;   /* Requires= in a circle: in the order of the list */
	} while ( waiting );
	strcpy( registry_plan_file, plan );
	option_override = saved_override;
	sprintf( text, T( "InstDone" ), done );
	for ( i = 0; i < ninst; i++ )
		if ( inst_on[i] && inst_state[i] != 1 && strlen( text ) + strlen( inst_label[i] ) + 60 < sizeof(text) )
			sprintf( text + strlen( text ), "\n%s: %s", inst_label[i],
				inst_state[i] == 3 ? T( "InstCancelled" ) :
				inst_state[i] == 4 ? T( "InstNeedDos" ) :
				inst_state[i] == 5 ? T( "InstNeeds" ) : T( "InstFailed" ) );
	ok[0] = T( "OK" );
	ui_clear();
	ui_message( T( "InstTitle" ), text, ok, 1, 0, 0 );
}

/* --- INSTALL /M: the installed packages (GDI.INI), some of them removed -- */
#define MANAGETEXT 1500

void manage_installed( void )
{
	static const char *b[2];
	char *text = malloc( MANAGETEXT );
	char p[80], l[200], dir[80], version[24], product[70], label[130], *e, *f;
	FILE *g;
	int in = 0, i, count = 0, removed = 0, rc;
	if ( !text )
		gdi_out_of_memory();
	sprintf( p, "%s\\GDI.INI", boot );
	ninst = 0;
	if ( ( g = fopen( p, "r" ) ) != NULL ) {
		while ( ninst < MAXINST && fgets( l, sizeof(l), g ) ) {
			strip_eol( l );
			if ( *l == '[' ) {
				in = !strnicmp( l, "[Installed]", 11 );
				continue;
			}
			if ( !in || !( e = strchr( l, '=' ) ) )
				continue;
			*e++ = 0;
			sprintf( dir, "%.79s", e );
			*version = 0;
			if ( ( f = strchr( dir, '|' ) ) != NULL ) {
				*f++ = 0;
				if ( ( f = strchr( f, '|' ) ) != NULL )
					sprintf( version, "%.23s", f + 1 );
			}
			path_cat( p, dir, "INSTALL.INI" );
			ini_peek( p, "Product", product, sizeof(product) );
			if ( !*product )
				sprintf( product, "%.69s", l );
			sprintf( label, "%s%s%s (%s)", product, *version ? " " : "", version, dir );
			inst_id[ninst] = str_dup( l );
			inst_path[ninst] = str_dup( dir );
			inst_label[ninst] = str_dup( label );
			inst_on[ninst] = inst_state[ninst] = 0;
			ninst++;
		}
		fclose( g );
	}
	b[0] = T( "OK" );
	if ( !ninst ) {
		ui_message( T( "ManageTitle" ), T( "ManageNone" ), b, 1, 0, 0 );
		free( text );
		return;
	}
	if ( ui_checklist( T( "ManageTitle" ), T( "ManageText" ),
		(const char **)inst_label, inst_on, ninst ) )
		abort_setup();
	for ( i = 0; i < ninst; i++ )
		count += inst_on[i];
	ui_clear();
	if ( !count ) {
		free( text );
		return;
	}
	sprintf( text, T( "ManageConfirm" ), count );
	b[0] = T( "Remove" );
	b[1] = T( "Cancel" );
	if ( ui_message( T( "ManageTitle" ), text, b, 2, 1, 0 ) ) {
		ui_clear();
		abort_setup();
	}
	/* the last installed first: a file replaced by two packages comes back */
	strcpy( dir, srcdir );
	for ( i = ninst - 1; i >= 0; i-- ) {
		if ( !inst_on[i] )
			continue;
		path_cat( p, inst_path[i], "INSTALL.INI" );
		ui_clear();
		ini_save();
		if ( ini_load( p ) ) {
			ini_restore();
			inst_state[i] = 2;
			continue;
		}
		if ( !stricmp( ini_get( "Setup", "Windows", "yes" ), "no" ) && windows_running() ) {
			ini_restore();
			inst_state[i] = 4;   /* only from MS-DOS */
			continue;
		}
		strcpy( srcdir, inst_path[i] );
		ui_title( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "" ) );
		in_extra = in_batch = uninstall = 1;
		keep_uninstaller = !stricmp( srcdir, dir );   /* this program's own directory */
		if ( ( rc = setjmp( extra_abort ) ) == 0 ) {
			remove_all();
			if ( gdi_removed_hook )
				gdi_removed_hook( inst_path[i] );
			inst_state[i] = 1;
			removed++;
		} else
			inst_state[i] = rc == 1 ? 3 : 2;
		in_extra = in_batch = uninstall = keep_uninstaller = 0;
		ini_restore();
		strcpy( srcdir, dir );
	}
	sprintf( text, T( "ManageDone" ), removed );
	for ( i = 0; i < ninst; i++ )
		if ( inst_on[i] && inst_state[i] != 1 && strlen( text ) + strlen( inst_label[i] ) + 60 < MANAGETEXT )
			sprintf( text + strlen( text ), "\n%s: %s", inst_label[i],
				inst_state[i] == 3 ? T( "InstCancelled" ) :
				inst_state[i] == 4 ? T( "InstNeedDos" ) : T( "InstFailed" ) );
	b[0] = T( "OK" );
	ui_clear();
	ui_title( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne Dual Installer" ) );
	ui_message( T( "ManageTitle" ), text, b, 1, 0, 0 );
	free( text );
}

void finish( void )
{
	static const char *b[2];
	const char *readme = ini_get( "Setup", "Readme", NULL );
	int rc;
	b[0] = T( "OK" );
	b[1] = T( "ShowReadme" );
	rc = ui_message( T( "DoneTitle" ),
		uninstall ? T( "UninstDone" )
		          : ini_get( "Setup", "Done", T( "DoneText" ) ),
		b, readme && !uninstall ? 2 : 1, 0, 0 );
	if ( rc == 1 ) {
		static const char *ok[1];
		ok[0] = T( "OK" );
		show_text( T( "ReadmeTitle" ), readme, ok, 1, 0 );
	}
	if ( dosmode ) {
		char msg[200];
		ui_clear();
		sprintf( msg, T( "DosDoneText" ), srcdir[0] );
		ask_reboot( msg );
	}
}
