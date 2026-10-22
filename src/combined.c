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
static char inst_on[MAXINST], inst_state[MAXINST];
static int ninst, cur_inst;

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
	char c[MAXOPT + 12];
	int i;
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

void run_installers( void )
{
	static char maindir[80], mainwin[80], plan[80], dir[80], ini[80];
	static char text[1500];
	static const char *ok[1];
	const char *saved_override = option_override;
	int i, rc, done = 0, pass;
	read_installers();
	if ( !ninst )
		return;
	if ( stricmp( ini_get( "Installer", "Select", "yes" ), "no" ) &&
		ui_checklist( T( "InstTitle" ),
		T( "InstText" ), (const char **)inst_label, inst_on, ninst ) )
		abort_setup();
	ui_clear();
	find_windows();
	if ( ui_input( T( "WinTitle" ),
		T( "WinText" ), windir, 64 ) )
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
			ui_clear();
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
	sprintf( text, T( "InstDone" ), done );
	for ( i = 0; i < ninst; i++ )
		if ( inst_on[i] && inst_state[i] != 1 && strlen( text ) + strlen( inst_label[i] ) + 60 < sizeof(text) )
			sprintf( text + strlen( text ), "\n%s: %s", inst_label[i],
				inst_state[i] == 3 ? T( "InstCancelled" ) :
				inst_state[i] == 4 ? T( "InstNeedDos" ) : T( "InstFailed" ) );
	ok[0] = T( "OK" );
	ui_clear();
	ui_message( T( "InstTitle" ), text, ok, 1, 0, 0 );
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
