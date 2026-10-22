/* journal.c - the records an installation leaves, shared by the DOS engine
 * and the Windows frontend (compiled for both): the entry of a package in
 * GDI.INI on the boot drive and the directories setup created (DIRS.LOG)
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <dos.h>
#include "gdi.h"

/* GDI.INI [Installed]: package-id=directory|options|version (OEM) */
int gdi_record_read( const char *boot, const char *key, char *dir, char *bits, char *version )
{
	char p[80], l[200];
	FILE *f;
	int in = 0, n, found = 0;
	sprintf( p, "%s\\GDI.INI", boot );
	n = strlen( key );
	if ( !( f = fopen( p, "r" ) ) )
		return 0;
	while ( !found && fgets( l, sizeof(l), f ) ) {
		strip_eol( l );
		if ( *l == '[' )
			in = !strnicmp( l, "[Installed]", 11 );
		else if ( in && !strnicmp( l, key, n ) && l[n] == '=' ) {
			char *a = l + n + 1, *b, *c;
			if ( ( b = strchr( a, '|' ) ) != NULL )
				*b++ = 0;
			else
				b = a + strlen( a );
			if ( ( c = strchr( b, '|' ) ) != NULL )
				*c++ = 0;
			else
				c = b + strlen( b );
			sprintf( dir, "%.79s", a );
			sprintf( bits, "%.16s", b );
			sprintf( version, "%.23s", c );
			found = 1;
		}
	}
	fclose( f );
	return found;
}

/* enters key=value in GDI.INI, or removes key (value NULL) */
void gdi_record_write( const char *boot, const char *key, const char *value )
{
	char p[80], t[80], l[200];
	FILE *in, *out;
	int n, section = 0, done = !value;
	sprintf( p, "%s\\GDI.INI", boot );
	strcpy( t, p );
	strcpy( t + strlen( t ) - 3, "$$$" );
	n = strlen( key );
	in = fopen( p, "r" );
	if ( !in && !value )
		return;
	if ( !( out = fopen( t, "w" ) ) ) {
		if ( in )
			fclose( in );
		return;
	}
	while ( in && fgets( l, sizeof(l), in ) ) {
		strip_eol( l );
		if ( *l == '[' ) {
			if ( section && !done ) {
				fprintf( out, "%s=%s\n", key, value );
				done = 1;
			}
			section = !strnicmp( l, "[Installed]", 11 );
		} else if ( section && !strnicmp( l, key, n ) && l[n] == '=' )
			continue;
		fprintf( out, "%s\n", l );
	}
	if ( !done ) {
		if ( !section )
			fprintf( out, "[Installed]\n" );
		fprintf( out, "%s=%s\n", key, value );
	}
	if ( in )
		fclose( in );
	if ( fclose( out ) ) {
		remove( t );
		return;
	}
	remove( p );
	rename( t, p );
}

/* the first parent of the target directory that setup creates: DIRS.LOG
 * names it, and the uninstaller removes the empty parents up to it */
void gdi_note_parents( const char *dir, char *top )
{
	char p[80];
	int i;
	top[0] = 0;
	strcpy( p, dir );
	for ( i = 3; p[i]; i++ )
		if ( p[i] == '\\' ) {
			p[i] = 0;
			if ( !file_exists( p ) ) {
				strcpy( top, p );
				return;
			}
			p[i] = '\\';
		}
}

/* a parent setup created that other packages still use: each of them (a
 * subdirectory with INSTALL.INI and without a DIRS.LOG of its own) gets
 * DIRS.LOG and removes the parents with its own directory later */
static void hand_over_parents( const char *parent, const char *top )
{
	struct find_t found;
	char p[80], q[80];
	FILE *f;
	path_cat( p, parent, "*.*" );
	if ( _dos_findfirst( p, _A_SUBDIR, &found ) )
		return;
	do {
		if ( !( found.attrib & _A_SUBDIR ) || found.name[0] == '.' )
			continue;
		path_cat( p, parent, found.name );
		path_cat( q, p, "DIRS.LOG" );
		strcat( p, "\\INSTALL.INI" );
		if ( !file_exists( p ) || file_exists( q ) )
			continue;
		if ( ( f = fopen( q, "w" ) ) != NULL ) {
			fprintf( f, "%s\n", top );
			fclose( f );
		}
	} while ( !_dos_findnext( &found ) );
}

/* removes the target directory dir (empty by now) and the empty parents
 * setup created for it (DIRS.LOG); a parent other packages still use goes
 * to them.  The current directory must be outside of dir. */
void gdi_remove_dirs( const char *dir )
{
	char p[80], top[80], *b;
	FILE *f;
	top[0] = 0;
	path_cat( p, dir, "DIRS.LOG" );
	if ( ( f = fopen( p, "r" ) ) != NULL ) {
		if ( fgets( top, sizeof(top), f ) )
			strip_eol( top );
		fclose( f );
	}
	/* still in use (journals for the Windows part): DIRS.LOG stays for
	 * the removal of dir later */
	remove( p );
	if ( rmdir( dir ) ) {
		if ( *top && ( f = fopen( p, "w" ) ) != NULL ) {
			fprintf( f, "%s\n", top );
			fclose( f );
		}
		return;
	}
	strcpy( p, dir );
	while ( ( !stricmp( p, dir ) || !rmdir( p ) ) && *top && stricmp( p, top ) &&
		( b = strrchr( p, '\\' ) ) != NULL && b - p > 2 &&
		!strnicmp( p, top, strlen( top ) ) )
		*b = 0;
	if ( *top && stricmp( p, dir ) && file_exists( p ) &&
		!strnicmp( p, top, strlen( top ) ) )
		hand_over_parents( p, top );
}
