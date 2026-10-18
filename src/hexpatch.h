/* hexpatch.h - checked binary patching for GeosOne DOS Installer
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#ifndef GDI_HEXPATCH_H
#define GDI_HEXPATCH_H

#define HEXPATCH_MAX_BYTES 32
#define HEXPATCH_OK         0
#define HEXPATCH_ALREADY    1
#define HEXPATCH_IO        -1
#define HEXPATCH_MISMATCH  -2
#define HEXPATCH_BAD_VALUE -3

int hexpatch_parse( const char *text, unsigned char *bytes, int max );
int hexpatch_file( const char *path, const unsigned char *before,
	const unsigned char *after, int length, int expected );

#endif
