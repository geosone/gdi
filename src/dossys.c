/* dossys.c - GeosOne Dual Installer, what only DOS has: programs run
 * with their exit status, the check for Windows, DOS mode (hard disk,
 * restart).  winsys.c has the same functions for the Windows frontend
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

/* Run a command line and return its exit status.  A program without a path
 * is taken from the directory containing INSTALL.EXE. */
int spawn_program( const char *cmdline )
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
