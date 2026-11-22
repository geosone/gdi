/* stub.c - the DOS part of SETUP.EXE (GeosOne Dual Installer)
 *
 * SETUP.EXE is the Windows program; started from DOS it runs INSTALL.EXE,
 * the DOS installer next to it, with the same arguments.
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <io.h>
#include <string.h>
#include <process.h>

int main( int argc, char **argv )
{
	static char path[144];
	static const char msg[] = "This program requires Microsoft Windows (INSTALL.EXE for DOS not found).\r\n";
	char *s;
	argc = argc;
	strncpy( path, argv[0], 130 );
	s = strrchr( path, '\\' );
	strcpy( s ? s + 1 : path, "INSTALL.EXE" );
	argv[0] = path;
	execv( path, (const char * const *)argv );
	write( 2, msg, sizeof(msg) - 1 );
	return 1;
}
