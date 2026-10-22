/* dosmode.c - GeosOne Dual Installer, DOS mode: hard disk, DOS itself,
 * further installations
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

void ask_reboot( const char *text )
{
	static const char *b[1];
	b[0] = T( "Restart", "Restart" );
	ui_message( T( "RestartTitle", "Restart" ), text, b, 1, 0, 0 );
	ui_exit();
	reboot();
}

/* the hard disk: partition (FDISK, then restart) and format it */
void dos_prepare( void )
{
	static const char *b[2];
	char msg[300];
	int st;
	b[1] = T( "Exit", "Exit" );
	_harderr( harderr_fail );
	while ( ( st = drive_state( boot[0] ) ) != 0 ) {
		ui_clear();
		if ( st == 2 ) {
			b[0] = T( "StartFdisk", "Start FDISK" );
			sprintf( msg, T( "FdiskText", "There is no drive %s yet. FDISK starts now: create a primary DOS partition (and make it active), then leave FDISK. The computer restarts and Setup continues." ), boot );
			if ( ui_message( T( "DiskSetupTitle", "Hard disk" ), msg, b, 2, 0, 0 ) )
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
			if ( ui_message( T( "DiskSetupTitle", "Hard disk" ), msg, b, 2, 0, 0 ) )
				abort_setup();
			sprintf( cmd, "%s", ini_get( "Setup", "Format", "FORMAT.COM %BOOT%" ) );
			run_program( cmd );
		}
	}
}


/* the setup steps of an INI (not DOS mode) */
/* A single choice, shared by DOS setup and the Windows frontend's DOS
 * transaction. Package files, not separate executable builds, carry text. */
void ask_language( void )
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
	selected = ui_message( T( "LanguageTitle", "Language" ),
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


void install_steps( void )
{
	char registry_error[130];
	if ( !in_batch )
		welcome();
	find_update();
	if ( !in_batch )   /* [Installer]: asked for all packages before */
		license();
	ui_clear();
	if ( !update_mode ) {
		ask_dirs();
		ui_clear();
		if ( in_batch )
			batch_choices();
		else {
			ask_options();
			ui_clear();
			ask_language();
		}
		ui_clear();
		if ( registry_write_plan( registry_error ) )
			fail( registry_error );
	}
	task_write_plan();   /* an update: only the new Program Manager items */
	if ( !in_batch && !confirm() )
		abort_setup();
	ui_clear();
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
void use_ini( const char *dir )
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
	ui_title( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne Dual Installer" ) );
	ui_clear();
}

/* further installations: the INSTALL.INI of another disk or directory is
 * installed by this program for the new system (%BOOT%), then the DOS
 * setup continues */
void extras( void )
{
	static const char *b[2];
	static char dir[80], ini[80], maindir[80], maindest[80];
	strcpy( maindir, srcdir );
	strcpy( maindest, dest );
	strcpy( dir, ini_get( "Setup", "ExtrasDir", "A:\\" ) );
	for ( ;; ) {
		b[0] = T( "InstallMore", "Install" );
		b[1] = T( "Finish", "Finish" );
		ui_clear();
		if ( ui_message( T( "ExtrasTitle", "Additional software" ),
			T( "ExtrasText", "Do you want to install additional software now (drivers, tools)? Insert its disk or enter the directory that contains its INSTALL.INI." ),
			b, 2, 0, 0 ) )
			break;
		ui_clear();
		if ( ui_input( T( "ExtrasTitle", "Additional software" ), T( "ExtrasDirText", "Directory with INSTALL.INI:" ), dir, 64 ) )
			continue;
		path_cat( ini, dir, "INSTALL.INI" );
		if ( !file_exists( ini ) ) {
			static const char *ok[] = { "OK" };
			char msg[120];
			sprintf( msg, "%s %s", T( "MissingFile", "File not found:" ), ini );
			ui_message( T( "ErrorTitle", "Error" ), msg, ok, 1, 0, 1 );
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
			ui_clear();
			{
				static const char *ok[1];
				ok[0] = T( "OK", "OK" );
				ui_message( T( "DoneTitle", "Setup complete" ),
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
