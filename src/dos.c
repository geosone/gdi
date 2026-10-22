/* dos.c - GeosOne Dual Installer (INSTALL.EXE): installs a DOS program
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
#include "ui.h"
#include "hexpatch.h"
#include "regtxn.h"
#include "progman.h"
#include "core.h"

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
	return core_run();
}
