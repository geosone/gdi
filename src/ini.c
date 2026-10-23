/* ini.c - INSTALL.INI and text files of the GeosOne Dual Installer
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
#include <malloc.h>
#ifdef __WINDOWS__
#include <windows.h>
#endif
#include "gdi.h"

#define MAXLINES 400

static char *line[MAXLINES];
static int   sect[MAXLINES];     /* index of the section header line */
static int   nline;
#ifdef __WINDOWS__
int ini_keep_oem;   /* the installer core: INSTALL.INI stays in the DOS code page */
#endif

char *str_dup( const char *s )
{
	char *p = malloc( strlen( s ) + 1 );
	if ( p )
		strcpy( p, s );
#ifndef __WINDOWS__
	else
		gdi_out_of_memory();   /* never go on with a lost line */
#endif
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
 * it may be gone); ini_restore() frees the other one and brings it back.
 * The lines wait in far memory: the heap of the DOS installer is in the
 * data segment, too small for two INI files */
static char __far * __far *saved_line;
static int __far *saved_sect;
static int saved_n;

static void __far *far_alloc( unsigned size )
{
	void __far *p = _fmalloc( size ? size : 1 );
#ifndef __WINDOWS__
	if ( !p )
		gdi_out_of_memory();
#endif
	return p;
}

void ini_save( void )
{
	int i;
	saved_line = far_alloc( nline * sizeof(char __far *) );
	saved_sect = far_alloc( nline * sizeof(int) );
	for ( i = 0; i < nline; i++ ) {
		saved_line[i] = far_alloc( strlen( line[i] ) + 1 );
		_fstrcpy( saved_line[i], line[i] );
		saved_sect[i] = sect[i];
	}
	saved_n = nline;
	ini_free();
}

void ini_restore( void )
{
	char buf[512];   /* as long as the lines of ini_load() */
	int i;
	ini_free();
	for ( i = 0; i < saved_n; i++ ) {
		_fstrcpy( (char __far *)buf, saved_line[i] );
		_ffree( saved_line[i] );
		line[i] = str_dup( buf );
		sect[i] = saved_sect[i];
	}
	nline = saved_n;
	_ffree( saved_line );
	_ffree( saved_sect );
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
	char buf[512];   /* long [Welcome] lines */
	int cur = -1;
	if ( !f )
		return -1;
	while ( nline < MAXLINES && fgets( buf, sizeof(buf), f ) ) {
		char *s = trim( buf );
		if ( *s == ';' || ( !*s && cur < 0 ) )
			continue;   /* empty lines are kept for ini_text() */
#ifdef __WINDOWS__
		if ( !ini_keep_oem )
			OemToAnsi( s, s );   /* INSTALL.INI is in the DOS code page */
#endif
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
/* the width of the text viewer: longer lines are wrapped */
#define TEXT_WIDTH 72

/* appends a copy of s (len characters) to the lines; 0 when out of memory */
static int text_add( ftext **l, int *n, int *max, const char *s, int len )
{
	char __far *p;
	if ( *n == *max ) {
		ftext *t = _frealloc( *l, ( *max * 2 ) * sizeof(char __far *) );
		if ( !t )
			return 0;
		*l = t;
		*max *= 2;
	}
	if ( !( p = _fmalloc( len + 1 ) ) )
		return 0;
	_fmemcpy( p, s, len );
	p[len] = 0;
	( *l )[( *n )++] = p;
	return 1;
}

/* a line wrapped (text, indentation): to lines[]; long lines are wrapped at
 * a space, the next lines keep the indentation.  The rest after the last
 * break goes to carry (it starts the next line of the paragraph); 0 when
 * out of memory */
static int text_wrap( ftext **l, int *n, int *max, const char *out, int o,
	int ind, char *carry, int *carry_ind )
{
	char part[200];
	int start = 0, b, w;
	while ( o - start > TEXT_WIDTH - ( start ? ind : 0 ) ) {
		w = TEXT_WIDTH - ( start ? ind : 0 );
		for ( b = start + w; b > start + ( start ? 0 : ind ) && out[b] != ' '; b-- )
			;
		if ( b <= start + ( start ? 0 : ind ) )
			b = start + w;   /* no space: cut */
		memset( part, ' ', start ? ind : 0 );
		memcpy( part + ( start ? ind : 0 ), out + start, b - start );
		if ( !text_add( l, n, max, part, ( start ? ind : 0 ) + b - start ) )
			return 0;
		for ( start = b; out[start] == ' '; start++ )
			;
	}
	if ( start ) {   /* the rest of a wrapped line */
		strcpy( carry, out + start );
		*carry_ind = ind;
		return 1;
	}
	return text_add( l, n, max, out, o );
}

ftext *text_load( const char *path, int *nlines )
{
	FILE *f = fopen( path, "r" );
	char buf[200], carry[200];
	ftext *l;
	int n = 0, max = 64, ok = 1, carry_ind = 0;
	if ( !f )
		return NULL;
	carry[0] = 0;
	l = _fmalloc( max * sizeof(char __far *) );
	while ( l && ok && fgets( buf, sizeof(buf), f ) ) {
		char out[200];
		int i, o = 0, ind = 0;
		for ( i = 0; buf[i] && buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0x1A && o < 190; i++ )
			if ( buf[i] == '\t' )
				do out[o++] = ' '; while ( o % 8 && o < 190 );
			else if ( buf[i] == '\f' )
				;
			else
				out[o++] = buf[i];
		out[o] = 0;
		while ( ind < o && ind < 24 && out[ind] == ' ' )
			ind++;
		if ( *carry ) {
			/* the next line of the paragraph starts with the rest */
			if ( ind < o && ind >= carry_ind && strlen( carry ) + o + 2 < sizeof(out) ) {
				char tmp[200];
				sprintf( tmp, "%.*s%s %s", ind, out, carry, out + ind );
				strcpy( out, tmp );
				o = strlen( out );
			} else {
				char tmp[200];
				sprintf( tmp, "%*s%s", carry_ind, "", carry );
				ok = text_add( &l, &n, &max, tmp, strlen( tmp ) );
			}
			*carry = 0;
		}
		if ( ok )
			ok = text_wrap( &l, &n, &max, out, o, ind, carry, &carry_ind );
	}
	if ( ok && *carry ) {
		char tmp[200];
		sprintf( tmp, "%*s%s", carry_ind, "", carry );
		ok = text_add( &l, &n, &max, tmp, strlen( tmp ) );
	}
	if ( l && ( !ok || !feof( f ) ) ) {   /* out of memory: no half license */
		text_free( l, n );
		l = NULL;
	}
	fclose( f );
	*nlines = n;
	return l;
}

void text_free( ftext *l, int n )
{
	while ( n )
		_ffree( l[--n] );
	_ffree( l );
}

/* the name of the package in the list of installations (%BOOT%\GDI.INI):
 * [Setup] Id=, else Product= */
void ini_package_id( char *out, unsigned size )
{
	const char *id = ini_get( "Setup", "Id", NULL );
	unsigned i;
	if ( !id )
		id = ini_get( "Setup", "Product", "Package" );
	for ( i = 0; id[i] && i + 1 < size; i++ )
		out[i] = id[i] == '=' || id[i] == '[' || id[i] == ';' ? '_' : id[i];
	out[i] = 0;
}
