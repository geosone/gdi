/* ini.c - INSTALL.INI and text files of the GeosOne DOS Installer
 *
 * The INI file is kept as a list of lines with the index of their section;
 * keys may occur several times in a section (e.g. Add=), [Files] and other
 * sections may also hold plain lines.  Comments start with ';'.
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gdi.h"

#define MAXLINES 400

static char *line[MAXLINES];
static int   sect[MAXLINES];     /* index of the section header line */
static int   nline;

char *str_dup( const char *s )
{
	char *p = malloc( strlen( s ) + 1 );
	if ( p )
		strcpy( p, s );
	return p;
}

static char *trim( char *s )
{
	char *e;
	while ( *s == ' ' || *s == '\t' )
		s++;
	e = s + strlen( s );
	while ( e > s && ( e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r' ) )
		*--e = 0;
	return s;
}

/* keep the loaded INI aside while another one is used (the disk with
 * it may be gone); ini_restore() frees the other one and brings it back */
static char **saved_line;
static int *saved_sect, saved_n;

void ini_save( void )
{
	saved_line = malloc( nline * sizeof(char *) );
	saved_sect = malloc( nline * sizeof(int) );
	memcpy( saved_line, line, nline * sizeof(char *) );
	memcpy( saved_sect, sect, nline * sizeof(int) );
	saved_n = nline;
	nline = 0;
}

void ini_restore( void )
{
	ini_free();
	memcpy( line, saved_line, saved_n * sizeof(char *) );
	memcpy( sect, saved_sect, saved_n * sizeof(int) );
	nline = saved_n;
	free( saved_line );
	free( saved_sect );
}

/* forget the loaded INI (to load another one) */
void ini_free( void )
{
	while ( nline )
		free( line[--nline] );
}

int ini_load( const char *path )
{
	FILE *f = fopen( path, "r" );
	char buf[256];
	int cur = -1;
	if ( !f )
		return -1;
	while ( nline < MAXLINES && fgets( buf, sizeof(buf), f ) ) {
		char *s = trim( buf );
		if ( *s == ';' || ( !*s && cur < 0 ) )
			continue;   /* empty lines are kept for ini_text() */
		line[nline] = str_dup( s );
		if ( *s == '[' )
			cur = nline;
		sect[nline++] = cur;
	}
	fclose( f );
	return 0;
}

static int in_section( int i, const char *section )
{
	int s = sect[i];
	int l = strlen( section );
	return s >= 0 && s != i && !strnicmp( line[s] + 1, section, l ) && line[s][l + 1] == ']';
}

/* next "key=value" in section from line pos on; returns the next position
 * (to continue) or -1 */
int ini_next( const char *section, const char *key, int pos, const char **value )
{
	int i, l = strlen( key );
	for ( i = pos; i < nline; i++ )
		if ( in_section( i, section ) && !strnicmp( line[i], key, l ) ) {
			const char *v = line[i] + l;
			while ( *v == ' ' )
				v++;
			if ( *v != '=' )
				continue;
			v++;
			while ( *v == ' ' )
				v++;
			*value = v;
			return i + 1;
		}
	return -1;
}

const char *ini_get( const char *section, const char *key, const char *def )
{
	const char *v;
	return ini_next( section, key, 0, &v ) >= 0 ? v : def;
}

/* plain lines of a section, including empty lines */
int ini_text( const char *section, int pos, const char **l )
{
	int i;
	for ( i = pos; i < nline; i++ )
		if ( in_section( i, section ) ) {
			*l = line[i];
			return i + 1;
		}
	return -1;
}

/* plain lines of a section */
int ini_lines( const char *section, int pos, const char **l )
{
	int i;
	for ( i = pos; i < nline; i++ )
		if ( line[i][0] && in_section( i, section ) ) {
			*l = line[i];
			return i + 1;
		}
	return -1;
}

/* text file as an array of lines (tabs expanded); NULL if not found */
char **text_load( const char *path, int *nlines )
{
	FILE *f = fopen( path, "r" );
	char buf[200], **l;
	int n = 0, max = 64;
	if ( !f )
		return NULL;
	l = malloc( max * sizeof(char *) );
	while ( l && fgets( buf, sizeof(buf), f ) ) {
		char out[200];
		int i, o = 0;
		for ( i = 0; buf[i] && buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0x1A && o < 190; i++ )
			if ( buf[i] == '\t' )
				do out[o++] = ' '; while ( o % 8 && o < 190 );
			else if ( buf[i] == '\f' )
				;
			else
				out[o++] = buf[i];
		out[o] = 0;
		if ( n == max ) {
			char **t = realloc( l, ( max *= 2 ) * sizeof(char *) );
			if ( !t )
				break;
			l = t;
		}
		l[n++] = str_dup( out );
	}
	fclose( f );
	*nlines = n;
	return l;
}

void text_free( char **l, int n )
{
	while ( n )
		free( l[--n] );
	free( l );
}
