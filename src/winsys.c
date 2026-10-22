/* winsys.c - GeosOne Dual Installer, the functions of dossys.c for the
 * installer core in the Windows frontend
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "gdi.h"
#include "ui.h"
#include "hexpatch.h"
#include "core.h"

/* Windows cannot tell the exit status of a program: 0 = it ran, -1 = it
 * could not be started.  The frontend uses the DOS engine for [Detect]. */
int spawn_program( const char *cmdline )
{
	char line[200], prog[200], *p;
	HINSTANCE task;
	MSG message;
	expand( line, cmdline, sizeof(line) );
	if ( ( p = strchr( line, ' ' ) ) != NULL )
		*p++ = 0;
	if ( !*line )
		return -1;
	if ( !strchr( line, '\\' ) && !strchr( line, ':' ) )
		path_cat( prog, srcdir, line );
	else
		strcpy( prog, line );
	if ( p && strlen( prog ) + strlen( p ) + 2 < sizeof(prog) ) {
		strcat( prog, " " );
		strcat( prog, p );
	}
	if ( ( task = (HINSTANCE)WinExec( prog, SW_SHOWNORMAL ) ) < (HINSTANCE)32 )
		return -1;
	while ( GetModuleUsage( task ) > 0 )
		if ( PeekMessage( &message, NULL, 0, 0, PM_REMOVE ) ) {
			TranslateMessage( &message );
			DispatchMessage( &message );
		} else
			Yield();
	return 0;
}

int windows_running( void )
{
	return 1;
}

void ask_reboot( const char *text )
{
	static const char *b[1];
	b[0] = T( "Restart" );
	ui_message( T( "RestartTitle" ), text, b, 1, 0, 0 );
	ui_exit();
	ExitWindows( EW_REBOOTSYSTEM, 0 );
	exit( 0 );
}

/* DOS mode runs only in the DOS engine */
void dos_prepare( void )
{
	fail( T( "NeedDos" ) );
}

/* Windows keeps an INI file it read in a cache: write it to disk first */
void profile_flush( const char *path )
{
	(void)path;
	WritePrivateProfileString( NULL, NULL, NULL, NULL );
}
