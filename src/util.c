/* util.c - small helpers shared by the DOS engine and the Windows frontend
 * (compiled for both)
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <string.h>
#include <io.h>
#include "gdi.h"

void path_cat( char *out, const char *dir, const char *name )
{
	strcpy( out, dir );
	if ( *out && out[strlen( out ) - 1] != '\\' )
		strcat( out, "\\" );
	strcat( out, name );
}

/* a file or a directory */
int file_exists( const char *path )
{
	return access( path, 0 ) == 0;
}

/* without CR, LF and the end-of-file mark Ctrl+Z */
void strip_eol( char *line )
{
	int length = strlen( line );
	while ( length && ( line[length - 1] == '\n' || line[length - 1] == '\r' ||
		line[length - 1] == 0x1A ) )
		line[--length] = 0;
}

void set_error( char *error, unsigned size, const char *text )
{
	if ( size ) {
		strncpy( error, text, size - 1 );
		error[size - 1] = 0;
	}
}
