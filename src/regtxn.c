/* regtxn.c - portable registry plan parser and lossless journal records
 * Copyright (C) 2026 GeosOne. GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "regtxn.h"

static int ci_equal( const char *a, const char *b )
{
    while ( *a && *b ) {
        if ( toupper( (unsigned char)*a ) != toupper( (unsigned char)*b ) )
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void set_error( char *error, size_t size, const char *text )
{
    if ( size ) {
        strncpy( error, text, size - 1 );
        error[size - 1] = 0;
    }
}

static char *trim( char *text )
{
    char *end;
    while ( *text == ' ' || *text == '\t' )
        text++;
    end = text + strlen( text );
    while ( end > text && ( end[-1] == ' ' || end[-1] == '\t' ||
                            end[-1] == '\r' || end[-1] == '\n' ) )
        *--end = 0;
    return text;
}

static int copy_field( char *out, size_t out_size, const char *text,
                       const char *what, char *error, size_t error_size )
{
    if ( strlen( text ) >= out_size ) {
        char message[96];
        sprintf( message, "%s is too long", what );
        set_error( error, error_size, message );
        return -1;
    }
    strcpy( out, text );
    return 0;
}

static int split_fields( const char *value, char *copy, size_t copy_size,
                         char **field, int max_fields )
{
    char *p;
    int count = 1;
    if ( strlen( value ) >= copy_size )
        return -1;
    strcpy( copy, value );
    field[0] = copy;
    for ( p = copy; *p; p++ )
        if ( *p == '|' ) {
            if ( count == max_fields )
                return max_fields + 1;
            *p = 0;
            field[count++] = p + 1;
        }
    return count;
}

static int condition_valid( const char *condition )
{
    const char *p = condition;
    while ( *p ) {
        int number = 0;
        while ( *p == ' ' || *p == ',' )
            p++;
        if ( !*p )
            break;
        if ( *p == '!' )
            p++;
        if ( *p == '?' ) {
            int length = 0;
            p++;
            while ( isalnum( (unsigned char)*p ) || *p == '_' || *p == '-' ) {
                length++;
                p++;
            }
            if ( !length || length > 15 )
                return 0;
        } else {
            if ( !isdigit( (unsigned char)*p ) )
                return 0;
            while ( isdigit( (unsigned char)*p ) ) {
                number = number * 10 + *p++ - '0';
                if ( number > 16 )
                    return 0;
            }
            if ( number < 1 )
                return 0;
        }
        while ( *p == ' ' )
            p++;
        if ( *p && *p != ',' )
            return 0;
    }
    return 1;
}

static int parse_root( const char *text )
{
    if ( ci_equal( text, "HKCR" ) ) return GDI_REG_HKCR;
    if ( ci_equal( text, "HKCU" ) ) return GDI_REG_HKCU;
    if ( ci_equal( text, "HKLM" ) ) return GDI_REG_HKLM;
    if ( ci_equal( text, "HKU" ) )  return GDI_REG_HKU;
    return 0;
}

static int parse_type( const char *text )
{
    if ( ci_equal( text, "SZ" ) )        return GDI_REG_SZ;
    if ( ci_equal( text, "EXPAND_SZ" ) ) return GDI_REG_EXPAND_SZ;
    if ( ci_equal( text, "BINARY" ) )    return GDI_REG_BINARY;
    if ( ci_equal( text, "DWORD" ) )     return GDI_REG_DWORD;
    return 0;
}

const char *gdi_reg_root_name( int root )
{
    switch ( root ) {
    case GDI_REG_HKCR: return "HKCR";
    case GDI_REG_HKCU: return "HKCU";
    case GDI_REG_HKLM: return "HKLM";
    case GDI_REG_HKU:  return "HKU";
    }
    return NULL;
}

const char *gdi_reg_type_name( int type )
{
    switch ( type ) {
    case GDI_REG_NONE:      return "NONE";
    case GDI_REG_SZ:        return "SZ";
    case GDI_REG_EXPAND_SZ: return "EXPAND_SZ";
    case GDI_REG_BINARY:    return "BINARY";
    case GDI_REG_DWORD:     return "DWORD";
    }
    return NULL;
}

int gdi_reg_parse( const char *verb, const char *value,
                   struct gdi_reg_entry *entry, char *error,
                   size_t error_size )
{
    char copy[1100];
    char *field[6];
    int count, wanted, i;

    memset( entry, 0, sizeof(*entry) );
    if ( ci_equal( verb, "Set" ) ) {
        entry->operation = GDI_REG_SET;
        wanted = 6;
    } else if ( ci_equal( verb, "DeleteValue" ) ) {
        entry->operation = GDI_REG_DELETE_VALUE;
        wanted = 4;
    } else if ( ci_equal( verb, "DeleteKey" ) ) {
        entry->operation = GDI_REG_DELETE_KEY;
        wanted = 3;
    } else {
        set_error( error, error_size, "unknown registry operation" );
        return -1;
    }
    count = split_fields( value, copy, sizeof(copy), field, 6 );
    if ( count != wanted ) {
        set_error( error, error_size, count < 0 ? "registry entry is too long" :
                   "wrong number of registry fields" );
        return -1;
    }
    for ( i = 0; i < count; i++ )
        field[i] = trim( field[i] );
    if ( !condition_valid( field[0] ) ) {
        set_error( error, error_size, "invalid registry condition" );
        return -1;
    }
    if ( copy_field( entry->condition, sizeof(entry->condition), field[0],
                     "registry condition", error, error_size ) )
        return -1;
    entry->root = parse_root( field[1] );
    if ( !entry->root ) {
        set_error( error, error_size, "unknown registry root" );
        return -1;
    }
    if ( !*field[2] ) {
        set_error( error, error_size, "registry key is empty" );
        return -1;
    }
    if ( copy_field( entry->key, sizeof(entry->key), field[2],
                     "registry key", error, error_size ) )
        return -1;
    if ( entry->operation != GDI_REG_DELETE_KEY ) {
        const char *name = !strcmp( field[3], "@" ) ? "" : field[3];
        if ( copy_field( entry->name, sizeof(entry->name), name,
                         "registry value name", error, error_size ) )
            return -1;
    }
    if ( entry->operation == GDI_REG_SET ) {
        entry->type = parse_type( field[4] );
        if ( !entry->type ) {
            set_error( error, error_size, "unknown registry value type" );
            return -1;
        }
        if ( copy_field( entry->text, sizeof(entry->text), field[5],
                         "registry data", error, error_size ) )
            return -1;
    }
    return 0;
}

int gdi_reg_validate_family( const struct gdi_reg_entry *entry, int win9x,
                             char *error, size_t error_size )
{
    if ( win9x )
        return 0;
    if ( entry->root != GDI_REG_HKCR ) {
        set_error( error, error_size, "Windows 3.x supports only HKCR" );
        return -1;
    }
    if ( entry->operation != GDI_REG_DELETE_KEY && entry->name[0] ) {
        set_error( error, error_size,
                   "Windows 3.x supports only the default value (@)" );
        return -1;
    }
    if ( entry->operation == GDI_REG_SET && entry->type != GDI_REG_SZ ) {
        set_error( error, error_size, "Windows 3.x supports only SZ values" );
        return -1;
    }
    return 0;
}

static int hex_value( int ch )
{
    if ( ch >= '0' && ch <= '9' ) return ch - '0';
    if ( ch >= 'a' && ch <= 'f' ) return ch - 'a' + 10;
    if ( ch >= 'A' && ch <= 'F' ) return ch - 'A' + 10;
    return -1;
}

int gdi_reg_make_data( const struct gdi_reg_entry *entry, unsigned char *data,
                       unsigned *data_size, char *error, size_t error_size )
{
    const char *p;
    unsigned count = 0;
    if ( entry->type == GDI_REG_SZ || entry->type == GDI_REG_EXPAND_SZ ) {
        size_t length = strlen( entry->text ) + 1;
        if ( length > GDI_REG_DATA_MAX ) {
            set_error( error, error_size, "registry string is too long" );
            return -1;
        }
        memcpy( data, entry->text, length );
        *data_size = (unsigned)length;
        return 0;
    }
    if ( entry->type == GDI_REG_DWORD ) {
        char *end;
        unsigned long value;
        if ( !*entry->text || entry->text[0] == '-' ) {
            set_error( error, error_size, "invalid DWORD data" );
            return -1;
        }
        errno = 0;
        value = strtoul( entry->text, &end, 0 );
        while ( *end == ' ' || *end == '\t' ) end++;
        if ( *end || errno == ERANGE
#if ULONG_MAX > 0xffffffffUL
             || value > 0xffffffffUL
#endif
           ) {
            set_error( error, error_size, "invalid DWORD data" );
            return -1;
        }
        data[0] = (unsigned char)value;
        data[1] = (unsigned char)( value >> 8 );
        data[2] = (unsigned char)( value >> 16 );
        data[3] = (unsigned char)( value >> 24 );
        *data_size = 4;
        return 0;
    }
    if ( entry->type != GDI_REG_BINARY ) {
        set_error( error, error_size, "registry entry has no data type" );
        return -1;
    }
    p = entry->text;
    while ( *p ) {
        int high, low;
        while ( *p == ' ' || *p == '\t' || *p == ',' || *p == '-' ) p++;
        if ( !*p ) break;
        high = hex_value( (unsigned char)*p++ );
        low = hex_value( (unsigned char)*p++ );
        if ( high < 0 || low < 0 || count == GDI_REG_DATA_MAX ) {
            set_error( error, error_size, "invalid BINARY data" );
            return -1;
        }
        data[count++] = (unsigned char)( high * 16 + low );
        if ( *p && *p != ' ' && *p != '\t' && *p != ',' && *p != '-' &&
             hex_value( (unsigned char)*p ) < 0 ) {
            set_error( error, error_size, "invalid BINARY separator" );
            return -1;
        }
    }
    *data_size = count;
    return 0;
}

static int hex_encode( const unsigned char *data, unsigned size, char *out,
                       size_t out_size )
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned i;
    if ( out_size < (size_t)size * 2 + 1 )
        return -1;
    for ( i = 0; i < size; i++ ) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 15];
    }
    out[size * 2] = 0;
    return 0;
}

static int hex_decode( const char *text, unsigned char *data, unsigned max,
                       unsigned *size )
{
    unsigned count = 0;
    while ( *text ) {
        int high = hex_value( (unsigned char)*text++ );
        int low = hex_value( (unsigned char)*text++ );
        if ( high < 0 || low < 0 || count == max )
            return -1;
        data[count++] = (unsigned char)( high * 16 + low );
    }
    *size = count;
    return 0;
}

int gdi_reg_journal_format( const struct gdi_reg_record *record, char *line,
                            size_t line_size )
{
    char formatted[GDI_REG_JOURNAL_MAX];
    char key[GDI_REG_KEY_MAX * 2 + 1];
    char name[GDI_REG_NAME_MAX * 2 + 1];
    char data[GDI_REG_DATA_MAX * 2 + 1];
    const char *root = gdi_reg_root_name( record->root );
    const char *type = gdi_reg_type_name( record->type );
    int length;
    if ( ( record->kind != 'V' && record->kind != 'K' ) || !root || !type ||
         record->existed < 0 || record->existed > 1 ||
         record->data_size > GDI_REG_DATA_MAX ||
         hex_encode( (const unsigned char *)record->key,
                     (unsigned)strlen( record->key ), key, sizeof(key) ) ||
         hex_encode( (const unsigned char *)record->name,
                     (unsigned)strlen( record->name ), name, sizeof(name) ) ||
         hex_encode( record->data, record->data_size, data, sizeof(data) ) )
        return -1;
    length = sprintf( formatted, "V1|%c|%s|%s|%s|%d|%s|%s",
                      record->kind, root, key, name, record->existed,
                      type, data );
    if ( length < 0 || (size_t)length >= line_size )
        return -1;
    strcpy( line, formatted );
    return 0;
}

int gdi_reg_journal_parse( const char *line, struct gdi_reg_record *record,
                           char *error, size_t error_size )
{
    char copy[GDI_REG_JOURNAL_MAX];
    char *field[8];
    unsigned length;
    int count;
    memset( record, 0, sizeof(*record) );
    count = split_fields( line, copy, sizeof(copy), field, 8 );
    if ( count == 8 )
        field[7] = trim( field[7] );
    if ( count != 8 || strcmp( field[0], "V1" ) ||
         ( strcmp( field[1], "V" ) && strcmp( field[1], "K" ) ) ) {
        set_error( error, error_size, "invalid registry journal record" );
        return -1;
    }
    record->kind = field[1][0];
    record->root = parse_root( field[2] );
    record->existed = !strcmp( field[5], "1" ) ? 1 :
                      !strcmp( field[5], "0" ) ? 0 : -1;
    record->type = parse_type( field[6] );
    if ( ci_equal( field[6], "NONE" ) ) record->type = GDI_REG_NONE;
    if ( !record->root || record->existed < 0 || !gdi_reg_type_name( record->type ) ||
         hex_decode( field[3], (unsigned char *)record->key, GDI_REG_KEY_MAX,
                     &length ) ) {
        set_error( error, error_size, "invalid registry journal metadata" );
        return -1;
    }
    record->key[length] = 0;
    if ( hex_decode( field[4], (unsigned char *)record->name, GDI_REG_NAME_MAX,
                     &length ) ) {
        set_error( error, error_size, "invalid registry journal value name" );
        return -1;
    }
    record->name[length] = 0;
    if ( hex_decode( field[7], record->data, GDI_REG_DATA_MAX,
                     &record->data_size ) ) {
        set_error( error, error_size, "invalid registry journal data" );
        return -1;
    }
    if ( record->kind == 'K' && ( record->name[0] || record->data_size ||
                                  record->type != GDI_REG_NONE ) ) {
        set_error( error, error_size, "invalid key journal record" );
        return -1;
    }
    if ( record->kind == 'V' && record->existed && record->type == GDI_REG_NONE ) {
        set_error( error, error_size, "existing value has no journal type" );
        return -1;
    }
    return 0;
}
