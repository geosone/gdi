/* hexpatch.c - signature-based binary patching with numbered backups
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "hexpatch.h"

static int hex_digit( int c )
{
	if ( c >= '0' && c <= '9' )
		return c - '0';
	c = toupper( c );
	return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int hexpatch_parse( const char *text, unsigned char *bytes, int max )
{
	int count = 0, high, low;
	while ( *text ) {
		while ( *text == ' ' || *text == '\t' || *text == '-' || *text == ',' )
			text++;
		if ( !*text )
			break;
		if ( count >= max || ( high = hex_digit( (unsigned char)*text++ ) ) < 0 ||
			( low = hex_digit( (unsigned char)*text++ ) ) < 0 )
			return HEXPATCH_BAD_VALUE;
		bytes[count++] = (unsigned char)( high * 16 + low );
		if ( *text && *text != ' ' && *text != '\t' && *text != '-' && *text != ',' )
			return HEXPATCH_BAD_VALUE;
	}
	return count ? count : HEXPATCH_BAD_VALUE;
}

/* KMP scan: works on files larger than a 16-bit near heap can hold. */
static int scan( FILE *file, const unsigned char *pattern, int length,
	long *offsets, int max_offsets )
{
	int failure[HEXPATCH_MAX_BYTES];
	int matched = 0, count = 0, c, i;
	long position = 0;

	failure[0] = 0;
	for ( i = 1; i < length; i++ ) {
		int j = failure[i - 1];
		while ( j && pattern[i] != pattern[j] )
			j = failure[j - 1];
		if ( pattern[i] == pattern[j] )
			j++;
		failure[i] = j;
	}
	while ( ( c = fgetc( file ) ) != EOF ) {
		while ( matched && (unsigned char)c != pattern[matched] )
			matched = failure[matched - 1];
		if ( (unsigned char)c == pattern[matched] )
			matched++;
		if ( matched == length ) {
			if ( count < max_offsets )
				offsets[count] = position - length + 1;
			count++;
			matched = failure[matched - 1];
		}
		position++;
	}
	return ferror( file ) ? HEXPATCH_IO : count;
}

static void side_name( char *out, const char *path, const char *extension )
{
	char *dot, *slash;
	strcpy( out, path );
	dot = strrchr( out, '.' );
	slash = strrchr( out, '\\' );
	if ( !slash )
		slash = strrchr( out, '/' );
	if ( !dot || ( slash && dot < slash ) )
		dot = out + strlen( out );
	strcpy( dot, extension );
}

static int exists( const char *path )
{
	FILE *file = fopen( path, "rb" );
	if ( !file )
		return 0;
	fclose( file );
	return 1;
}

static int copy_file( const char *from, const char *to )
{
	FILE *input = fopen( from, "rb" ), *output;
	unsigned char buffer[4096];
	size_t count;
	int error;
	if ( !input )
		return HEXPATCH_IO;
	output = fopen( to, "wb" );
	if ( !output ) {
		fclose( input );
		return HEXPATCH_IO;
	}
	while ( ( count = fread( buffer, 1, sizeof(buffer), input ) ) != 0 )
		if ( fwrite( buffer, 1, count, output ) != count ) {
			fclose( input );
			fclose( output );
			return HEXPATCH_IO;
		}
	error = ferror( input );
	if ( fclose( input ) )
		error = 1;
	if ( fclose( output ) )
		error = 1;
	return error ? HEXPATCH_IO : HEXPATCH_OK;
}

static int make_backup( const char *path, char *backup )
{
	char extension[5];
	int number;
	for ( number = 1; number < 100; number++ ) {
		sprintf( extension, ".G%02d", number );
		side_name( backup, path, extension );
		if ( !exists( backup ) )
			return copy_file( path, backup );
	}
	return HEXPATCH_IO;
}

static int write_patched( const char *path, const char *temporary,
	const unsigned char *before, const unsigned char *after, int length,
	const long *offsets, int expected )
{
	FILE *input = fopen( path, "rb" ), *output;
	long position = 0;
	int match = 0, c, i;
	int error;
	if ( !input )
		return HEXPATCH_IO;
	output = fopen( temporary, "wb" );
	if ( !output ) {
		fclose( input );
		return HEXPATCH_IO;
	}
	while ( ( c = fgetc( input ) ) != EOF ) {
		if ( match < expected && position == offsets[match] ) {
			if ( (unsigned char)c != before[0] )
				break;
			for ( i = 1; i < length; i++ )
				if ( ( c = fgetc( input ) ) == EOF || (unsigned char)c != before[i] )
					break;
			if ( i != length || fwrite( after, 1, length, output ) != (size_t)length )
				break;
			position += length;
			match++;
		} else {
			if ( fputc( c, output ) == EOF )
				break;
			position++;
		}
	}
	if ( ferror( input ) || ferror( output ) || match != expected ) {
		fclose( input );
		fclose( output );
		remove( temporary );
		return HEXPATCH_IO;
	}
	error = fclose( input );
	if ( fclose( output ) )
		error = 1;
	if ( error ) {
		remove( temporary );
		return HEXPATCH_IO;
	}
	return HEXPATCH_OK;
}

int hexpatch_file( const char *path, const unsigned char *before,
	const unsigned char *after, int length, int expected )
{
	FILE *file;
	long offsets[32], unused[1];
	char temporary[80], backup[80];
	int before_count, after_count, result;

	if ( length < 1 || length > HEXPATCH_MAX_BYTES || expected < 1 || expected > 32 ||
		!memcmp( before, after, length ) )
		return HEXPATCH_BAD_VALUE;
	file = fopen( path, "rb" );
	if ( !file )
		return HEXPATCH_IO;
	before_count = scan( file, before, length, offsets, 32 );
	rewind( file );
	after_count = scan( file, after, length, unused, 0 );
	fclose( file );
	if ( before_count < 0 || after_count < 0 )
		return HEXPATCH_IO;
	if ( before_count == 0 && after_count == expected )
		return HEXPATCH_ALREADY;
	if ( before_count != expected || after_count != 0 )
		return HEXPATCH_MISMATCH;
	for ( result = 1; result < expected; result++ )
		if ( offsets[result] < offsets[result - 1] + length )
			return HEXPATCH_MISMATCH;

	side_name( temporary, path, ".$$$" );
	remove( temporary );
	result = write_patched( path, temporary, before, after, length, offsets, expected );
	if ( result != HEXPATCH_OK )
		return result;
	result = make_backup( path, backup );
	if ( result != HEXPATCH_OK ) {
		remove( temporary );
		return result;
	}
	if ( remove( path ) || rename( temporary, path ) ) {
		remove( temporary );
		if ( !exists( path ) )
			copy_file( backup, path );
		return HEXPATCH_IO;
	}
	return HEXPATCH_OK;
}
