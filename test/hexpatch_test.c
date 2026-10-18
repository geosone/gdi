#include <stdio.h>
#include <string.h>
#include "hexpatch.h"

static int write_sample( const char *path )
{
	static const unsigned char data[] = {
		0x10, 0x66, 0xC7, 0x46, 0x49, 0xFF, 0xFF, 0x20,
		0x66, 0xC7, 0x46, 0x49, 0xFF, 0xFF, 0x30
	};
	FILE *file = fopen( path, "wb" );
	if ( !file )
		return 1;
	if ( fwrite( data, 1, sizeof(data), file ) != sizeof(data) )
		return 1;
	return fclose( file ) != 0;
}

static int count_pattern( const char *path, const unsigned char *pattern, int length )
{
	FILE *file = fopen( path, "rb" );
	unsigned char window[HEXPATCH_MAX_BYTES];
	int used = 0, count = 0, c;
	if ( !file )
		return -1;
	while ( ( c = fgetc( file ) ) != EOF ) {
		if ( used < length )
			window[used++] = (unsigned char)c;
		else {
			memmove( window, window + 1, length - 1 );
			window[length - 1] = (unsigned char)c;
		}
		if ( used == length && !memcmp( window, pattern, length ) )
			count++;
	}
	fclose( file );
	return count;
}

int main( int argc, char **argv )
{
	unsigned char before[HEXPATCH_MAX_BYTES], after[HEXPATCH_MAX_BYTES];
	char path[256], backup[256];
	int length;
	if ( argc != 2 )
		return 10;
	sprintf( path, "%s/WIN386.EXE", argv[1] );
	sprintf( backup, "%s/WIN386.G01", argv[1] );
	length = hexpatch_parse( "66 C7 46 49 FF FF", before, sizeof(before) );
	if ( length != 6 || hexpatch_parse( "6A-FF-8F-46-49-90", after, sizeof(after) ) != length )
		return 11;
	if ( hexpatch_parse( "0", after, sizeof(after) ) != HEXPATCH_BAD_VALUE )
		return 12;
	hexpatch_parse( "6A FF 8F 46 49 90", after, sizeof(after) );
	if ( write_sample( path ) )
		return 13;
	if ( hexpatch_probe( path, before, after, length, 2 ) != HEXPATCH_OK )
		return 20;
	if ( hexpatch_file( path, before, after, length, 1 ) != HEXPATCH_MISMATCH )
		return 14;
	if ( hexpatch_file( path, before, after, length, 2 ) != HEXPATCH_OK )
		return 15;
	if ( count_pattern( path, after, length ) != 2 || count_pattern( backup, before, length ) != 2 )
		return 16;
	if ( hexpatch_file( path, before, after, length, 2 ) != HEXPATCH_ALREADY )
		return 17;
	if ( hexpatch_probe( path, before, after, length, 2 ) != HEXPATCH_ALREADY )
		return 21;
	if ( hexpatch_file( path, after, before, length, 2 ) != HEXPATCH_OK )
		return 18;
	if ( count_pattern( path, before, length ) != 2 )
		return 19;
	return 0;
}
