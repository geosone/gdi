/* lang.c - the texts of the installer: [Text] of INSTALL.INI, else the
 * text of the language of the system (src/language.ini, build/language.h),
 * else the English one.  The language: [Setup] Texts= or /L:, else
 * [intl] of WIN.INI under Windows, else COUNTRY= of DOS.
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#ifdef __WINDOWS__
#include <windows.h>
#endif
#include "gdi.h"
#include "ui.h"
#include "language.h"

static int language = -1;          /* section of language.ini; -1: not chosen yet */
static char *cache[GDI_TEXTS];     /* the texts in use, near copies */

static void forget( void )
{
	int i;
	for ( i = 0; i < GDI_TEXTS; i++ )
		if ( cache[i] ) {
			free( cache[i] );
			cache[i] = NULL;
		}
}

/* ",item," in a list ",a,b," of language.h */
static int listed( const char __far *list, const char *item )
{
	char want[12];
	if ( !*item || strlen( item ) > 9 )
		return 0;
	sprintf( want, ",%s,", item );
	strlwr( want );
	return _fstrstr( list, (const char __far *)want ) != NULL;
}

int gdi_text_language( const char *name )
{
	int i;
	for ( i = 0; i < GDI_LANGUAGES; i++ )
		if ( !_fstricmp( gdi_language[i][0], (const char __far *)name ) ) {
			if ( i != language )
				forget();
			language = i;
			return 0;
		}
	return -1;
}

static void choose( void )
{
	char item[10];
	const char *v = ini_get( "Setup", "Texts", NULL );
	int i, country;
	language = 0;
	if ( v && !gdi_text_language( v ) )
		return;
#ifdef __WINDOWS__
	GetProfileString( "intl", "sLanguage", "", item, sizeof(item) );
	for ( i = 1; i < GDI_LANGUAGES; i++ )
		if ( listed( gdi_language[i][2], item ) ) {
			language = i;
			return;
		}
	country = GetProfileInt( "intl", "iCountry", 1 );
#else
	{
		static char info[34];   /* DOS writes the country information here */
		union REGS r;
		r.x.ax = 0x3800;        /* the country of COUNTRY= */
		r.x.dx = (unsigned)info;
		intdos( &r, &r );
		country = r.x.cflag ? 1 : r.x.bx;
	}
#endif
	sprintf( item, "%d", country );
	for ( i = 1; i < GDI_LANGUAGES; i++ )
		if ( listed( gdi_language[i][1], item ) ) {
			language = i;
			return;
		}
}

const char *gdi_text_name( void )
{
	static char name[20];
	if ( language < 0 )
		choose();
	_fstrncpy( (char __far *)name, gdi_language[language][0], sizeof(name) - 1 );
	return name;
}

const char *gdi_text( const char *key )
{
	char *v = (char *)ini_get( "Text", key, NULL ), *p;
	int low = 0, high = GDI_TEXTS - 1;
	if ( v ) {   /* "\n" = new line */
		while ( ( p = strstr( v, "\\n" ) ) != NULL ) {
			*p = '\n';
			memmove( p + 1, p + 2, strlen( p + 2 ) + 1 );
		}
		return v;
	}
	if ( language < 0 )
		choose();
	while ( low <= high ) {
		int i = ( low + high ) / 2, c = _fstrcmp( (const char __far *)key, gdi_text_key[i] );
		if ( c < 0 )
			high = i - 1;
		else if ( c > 0 )
			low = i + 1;
		else {
			const char __far *t;
			if ( cache[i] )
				return cache[i];
			t = gdi_text_of[language][i] ? gdi_text_of[language][i] : gdi_text_of[0][i];
			if ( ( cache[i] = malloc( _fstrlen( t ) + 1 ) ) == NULL ) {
				static char spare[120];   /* the heap is full: "NoMemory" */
				_fstrncpy( (char __far *)spare, t, sizeof(spare) - 1 );
				return spare;
			}
			_fstrcpy( (char __far *)cache[i], t );
#ifdef __WINDOWS__
			if ( !ini_keep_oem )   /* the frontend: INSTALL.INI in ANSI */
				OemToAnsi( cache[i], cache[i] );
#endif
			return cache[i];
		}
	}
	return key;   /* not a key of language.ini: an error text itself */
}

/* the section of language.ini for an sLanguage= or a country code; -1 */
int gdi_language_find( const char *slanguage, int country )
{
	char item[8];
	int i;
	for ( i = 0; i < GDI_LANGUAGES; i++ )
		if ( listed( gdi_language[i][2], slanguage ) )
			return i;
	sprintf( item, "%d", country );
	for ( i = 0; i < GDI_LANGUAGES; i++ )
		if ( listed( gdi_language[i][1], item ) )
			return i;
	return -1;
}

/* the text key of section lang (-1: the installer's), in the DOS code page */
void gdi_text_copy( int lang, const char *key, char *out, int size )
{
	int i;
	if ( lang < 0 ) {
		if ( language < 0 )
			choose();
		lang = language;
	}
	for ( i = 0; i < GDI_TEXTS; i++ )
		if ( !_fstrcmp( (const char __far *)key, gdi_text_key[i] ) ) {
			const char __far *t = gdi_text_of[lang][i] ? gdi_text_of[lang][i] : gdi_text_of[0][i];
			_fstrncpy( (char __far *)out, t, size - 1 );
			out[size - 1] = 0;
			return;
		}
	sprintf( out, "%.*s", size - 1, key );
}

/* the frontend changes between ANSI and the DOS code page */
void gdi_text_reset( void )
{
	forget();
}
