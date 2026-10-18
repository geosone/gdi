/* winreg16.c - transactional Windows 3.1 registration-database backend
 * Copyright (C) 2026 GeosOne. GNU General Public License version 3. */

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include "regtxn.h"
#include "winreg16.h"

#define ERROR_NO_MORE_ITEMS_16 259L
#define ERROR_MORE_DATA_16     234L
#define MAX_JOURNAL_RECORDS    400

static struct gdi_reg_entry reg_entry;
static struct gdi_reg_record reg_record;
static unsigned char reg_data[GDI_REG_DATA_MAX];
static char reg_line[1200];
static long record_offset[MAX_JOURNAL_RECORDS];

static void set_error( char *error, unsigned size, const char *text )
{
    if ( size ) {
        strncpy( error, text, size - 1 );
        error[size - 1] = 0;
    }
}

static void api_error( char *error, unsigned size, const char *action,
                       LONG result )
{
    char message[150];
    sprintf( message, "%s (registry error %ld)", action, result );
    set_error( error, size, message );
}

static int append_record( FILE *journal, const struct gdi_reg_record *record,
                          char *error, unsigned error_size )
{
    char line[GDI_REG_JOURNAL_MAX];
    if ( gdi_reg_journal_format( record, line, sizeof(line) ) ||
         fprintf( journal, "%s\n", line ) < 0 || fflush( journal ) ) {
        set_error( error, error_size, "Cannot write the registry journal" );
        return -1;
    }
    return 0;
}

static LONG open_key( const char *key, HKEY FAR *handle )
{
    return RegOpenKey( HKEY_CLASSES_ROOT, key, handle );
}

static int query_default( HKEY handle, struct gdi_reg_record *record,
                          char *error, unsigned error_size )
{
    LONG size = GDI_REG_DATA_MAX;
    LONG result;
    result = RegQueryValue( handle, "", (LPSTR)record->data, &size );
    if ( result == ERROR_BADKEY )
        return 0;
    if ( result != ERROR_SUCCESS ) {
        api_error( error, error_size, "Cannot read existing registry value", result );
        return -1;
    }
    if ( size < 0 || size > GDI_REG_DATA_MAX ) {
        set_error( error, error_size, "Existing registry value is too large" );
        return -1;
    }
    record->existed = 1;
    record->type = GDI_REG_SZ;
    record->data_size = (unsigned)size;
    if ( !record->data_size || record->data[record->data_size - 1] != 0 ) {
        if ( record->data_size == GDI_REG_DATA_MAX ) {
            set_error( error, error_size, "Existing registry value is too large" );
            return -1;
        }
        record->data[record->data_size++] = 0;
    }
    return 1;
}

static int key_has_subkeys( HKEY handle, char *error, unsigned error_size )
{
    char name[2];
    LONG result = RegEnumKey( handle, 0, name, sizeof(name) );
    if ( result == ERROR_SUCCESS || result == ERROR_MORE_DATA_16 )
        return 1;
    if ( result == ERROR_NO_MORE_ITEMS_16 )
        return 0;
    api_error( error, error_size, "Cannot enumerate registry key", result );
    return -1;
}

/* Create every missing path component separately.  Each creation is recorded
 * before it occurs, so reverse replay removes child before parent. */
static int ensure_key( const char *key, FILE *journal, char *error,
                       unsigned error_size )
{
    char path[GDI_REG_KEY_MAX + 1];
    char *p;
    HKEY handle;
    strcpy( path, key );
    p = path;
    for ( ;; ) {
        LONG result;
        char saved;
        while ( *p && *p != '\\' )
            p++;
        saved = *p;
        *p = 0;
        result = open_key( path, &handle );
        if ( result == ERROR_SUCCESS )
            RegCloseKey( handle );
        else if ( result == ERROR_BADKEY ) {
            memset( &reg_record, 0, sizeof(reg_record) );
            reg_record.kind = 'K';
            reg_record.root = GDI_REG_HKCR;
            reg_record.type = GDI_REG_NONE;
            strcpy( reg_record.key, path );
            if ( append_record( journal, &reg_record, error, error_size ) )
                return -1;
            result = RegCreateKey( HKEY_CLASSES_ROOT, path, &handle );
            if ( result != ERROR_SUCCESS ) {
                api_error( error, error_size, "Cannot create registry key", result );
                return -1;
            }
            RegCloseKey( handle );
        } else {
            api_error( error, error_size, "Cannot open registry key", result );
            return -1;
        }
        *p = saved;
        if ( !saved )
            break;
        p++;
    }
    return 0;
}

static int journal_value( const struct gdi_reg_entry *entry, HKEY handle,
                          FILE *journal, int require_existing,
                          char *error, unsigned error_size )
{
    int queried;
    memset( &reg_record, 0, sizeof(reg_record) );
    reg_record.kind = 'V';
    reg_record.root = GDI_REG_HKCR;
    reg_record.type = GDI_REG_NONE;
    strcpy( reg_record.key, entry->key );
    queried = query_default( handle, &reg_record, error, error_size );
    if ( queried < 0 )
        return -1;
    if ( require_existing && !queried ) {
        set_error( error, error_size,
                   "Windows 3.x cannot preserve a missing default value" );
        return -1;
    }
    return append_record( journal, &reg_record, error, error_size );
}

static int apply_set( const struct gdi_reg_entry *entry, FILE *journal,
                      char *error, unsigned error_size )
{
    HKEY handle;
    LONG result = open_key( entry->key, &handle );
    int existed = result == ERROR_SUCCESS;
    unsigned size;
    if ( result != ERROR_SUCCESS && result != ERROR_BADKEY ) {
        api_error( error, error_size, "Cannot open registry key", result );
        return -1;
    }
    if ( existed ) {
        if ( journal_value( entry, handle, journal, 1, error, error_size ) ) {
            RegCloseKey( handle );
            return -1;
        }
        RegCloseKey( handle );
    } else if ( ensure_key( entry->key, journal, error, error_size ) )
        return -1;
    if ( !existed ) {
        if ( open_key( entry->key, &handle ) != ERROR_SUCCESS ) {
            set_error( error, error_size, "Created registry key cannot be opened" );
            return -1;
        }
        if ( journal_value( entry, handle, journal, 0, error, error_size ) ) {
            RegCloseKey( handle );
            return -1;
        }
        RegCloseKey( handle );
    }
    if ( gdi_reg_make_data( entry, reg_data, &size, error, error_size ) )
        return -1;
    result = RegSetValue( HKEY_CLASSES_ROOT, entry->key, REG_SZ,
                          (LPCSTR)reg_data, size );
    if ( result != ERROR_SUCCESS ) {
        api_error( error, error_size, "Cannot set registry value", result );
        return -1;
    }
    return 0;
}

static int apply_delete( const struct gdi_reg_entry *entry, FILE *journal,
                         char *error, unsigned error_size )
{
    HKEY handle;
    LONG result = open_key( entry->key, &handle );
    int has_value, subkeys;
    if ( result == ERROR_BADKEY )
        return 0;
    if ( result != ERROR_SUCCESS ) {
        api_error( error, error_size, "Cannot open registry key", result );
        return -1;
    }
    memset( &reg_record, 0, sizeof(reg_record) );
    reg_record.kind = 'V';
    reg_record.root = GDI_REG_HKCR;
    reg_record.type = GDI_REG_NONE;
    strcpy( reg_record.key, entry->key );
    has_value = query_default( handle, &reg_record, error, error_size );
    if ( has_value < 0 ) {
        RegCloseKey( handle );
        return -1;
    }
    if ( entry->operation == GDI_REG_DELETE_VALUE && !has_value ) {
        RegCloseKey( handle );
        return 0;
    }
    subkeys = key_has_subkeys( handle, error, error_size );
    RegCloseKey( handle );
    if ( subkeys < 0 )
        return -1;
    if ( subkeys ) {
        set_error( error, error_size,
                   entry->operation == GDI_REG_DELETE_VALUE ?
                   "Windows 3.x cannot delete a value from a key with subkeys" :
                   "Registry key has subkeys and will not be deleted" );
        return -1;
    }
    if ( has_value && append_record( journal, &reg_record, error, error_size ) )
        return -1;
    memset( &reg_record, 0, sizeof(reg_record) );
    reg_record.kind = 'K';
    reg_record.root = GDI_REG_HKCR;
    reg_record.existed = 1;
    reg_record.type = GDI_REG_NONE;
    strcpy( reg_record.key, entry->key );
    if ( append_record( journal, &reg_record, error, error_size ) )
        return -1;
    result = RegDeleteKey( HKEY_CLASSES_ROOT, entry->key );
    if ( result != ERROR_SUCCESS ) {
        api_error( error, error_size, "Cannot delete registry key", result );
        return -1;
    }
    return 0;
}

static int restore_record( const struct gdi_reg_record *record, char *error,
                           unsigned error_size )
{
    HKEY handle;
    LONG result;
    if ( record->kind == 'V' ) {
        if ( !record->existed )
            return 0; /* a following reverse K record removes the new key */
        result = RegSetValue( HKEY_CLASSES_ROOT, record->key, REG_SZ,
                              (LPCSTR)record->data, record->data_size );
        if ( result != ERROR_SUCCESS ) {
            api_error( error, error_size, "Cannot restore registry value", result );
            return -1;
        }
        return 0;
    }
    if ( record->existed ) {
        result = RegCreateKey( HKEY_CLASSES_ROOT, record->key, &handle );
        if ( result == ERROR_SUCCESS )
            RegCloseKey( handle );
    } else {
        result = RegDeleteKey( HKEY_CLASSES_ROOT, record->key );
        if ( result == ERROR_BADKEY )
            result = ERROR_SUCCESS;
    }
    if ( result != ERROR_SUCCESS ) {
        api_error( error, error_size, "Cannot restore registry key", result );
        return -1;
    }
    return 0;
}

static int record_supported( const struct gdi_reg_record *record )
{
    if ( record->root != GDI_REG_HKCR )
        return 0;
    if ( record->kind == 'K' )
        return record->type == GDI_REG_NONE;
    return record->existed ? record->type == GDI_REG_SZ :
                             record->type == GDI_REG_NONE;
}

static int restore_from( const char *journal_path, long start, char *error,
                         unsigned error_size )
{
    FILE *file = fopen( journal_path, "rb" );
    int count = 0, i;
    if ( !file ) {
        set_error( error, error_size, "Cannot read the registry journal" );
        return -1;
    }
    if ( fseek( file, start, SEEK_SET ) ) {
        fclose( file );
        set_error( error, error_size, "Cannot seek in the registry journal" );
        return -1;
    }
    while ( count < MAX_JOURNAL_RECORDS ) {
        long offset = ftell( file );
        if ( !fgets( reg_line, sizeof(reg_line), file ) )
            break;
        record_offset[count++] = offset;
    }
    if ( !feof( file ) ) {
        fclose( file );
        set_error( error, error_size, "Registry journal has too many records" );
        return -1;
    }
    for ( i = count - 1; i >= 0; i-- ) {
        if ( fseek( file, record_offset[i], SEEK_SET ) ||
             !fgets( reg_line, sizeof(reg_line), file ) ) {
            set_error( error, error_size, "Cannot read the registry journal" );
            fclose( file );
            return -1;
        }
        if ( gdi_reg_journal_parse( reg_line, &reg_record,
                                    error, error_size ) ) {
            fclose( file );
            return -1;
        }
        if ( !record_supported( &reg_record ) ) {
            set_error( error, error_size,
                       "Registry journal is not valid for Windows 3.x" );
            fclose( file );
            return -1;
        }
        if ( restore_record( &reg_record, error, error_size ) ) {
            fclose( file );
            return -1;
        }
    }
    fclose( file );
    return 0;
}

int gdi_reg16_restore_journal( const char *journal_path, char *error,
                               unsigned error_size )
{
    return restore_from( journal_path, 0L, error, error_size );
}

static int plan_header( FILE *plan, char *journal, char *error,
                        unsigned error_size )
{
    if ( !fgets( reg_line, sizeof(reg_line), plan ) ||
         strcmp( reg_line, "GDI-REGISTRY-PLAN 1\n" ) ||
         !fgets( reg_line, sizeof(reg_line), plan ) ||
         strncmp( reg_line, "Journal=", 8 ) ) {
        set_error( error, error_size, "Invalid registry plan header" );
        return -1;
    }
    if ( strlen( reg_line + 8 ) >= 128 ) {
        set_error( error, error_size, "Registry journal path is too long" );
        return -1;
    }
    strcpy( journal, reg_line + 8 );
    journal[strcspn( journal, "\r\n" )] = 0;
    if ( !*journal ) {
        set_error( error, error_size, "Registry journal path is empty" );
        return -1;
    }
    return 0;
}

static int validate_plan( const char *plan_path, char *journal, int *entries,
                          char *error, unsigned error_size )
{
    FILE *plan = fopen( plan_path, "r" );
    int line_number = 2;
    if ( !plan ) {
        set_error( error, error_size, "Cannot read the registry plan" );
        return -1;
    }
    if ( plan_header( plan, journal, error, error_size ) ) {
        fclose( plan );
        return -1;
    }
    *entries = 0;
    while ( fgets( reg_line, sizeof(reg_line), plan ) ) {
        char detail[100];
        unsigned size;
        line_number++;
        if ( gdi_reg_parse_line( reg_line, &reg_entry, detail, sizeof(detail) ) ||
             gdi_reg_validate_family( &reg_entry, 0, detail, sizeof(detail) ) ||
             ( reg_entry.operation == GDI_REG_SET &&
               gdi_reg_make_data( &reg_entry, reg_data, &size,
                                  detail, sizeof(detail) ) ) ) {
            sprintf( error, "Registry plan line %d: %.90s", line_number, detail );
            fclose( plan );
            return -1;
        }
        ( *entries )++;
    }
    fclose( plan );
    return 0;
}

static int validate_journal( const char *path, char *error,
                             unsigned error_size )
{
    FILE *file = fopen( path, "rb" );
    if ( !file )
        return 0;
    while ( fgets( reg_line, sizeof(reg_line), file ) ) {
        if ( gdi_reg_journal_parse( reg_line, &reg_record,
                                    error, error_size ) ) {
            fclose( file );
            return -1;
        }
        if ( !record_supported( &reg_record ) ) {
                set_error( error, error_size,
                           "Registry journal is not valid for Windows 3.x" );
            fclose( file );
            return -1;
        }
    }
    fclose( file );
    return 0;
}

int gdi_reg16_apply_plan( const char *plan_path, char *error,
                          unsigned error_size )
{
    char journal_path[128];
    FILE *plan, *journal;
    long journal_start;
    int entries, failed = 0;
    if ( validate_plan( plan_path, journal_path, &entries,
                        error, error_size ) )
        return -1;
    if ( !entries )
        return 0;
    if ( validate_journal( journal_path, error, error_size ) )
        return -1;
    journal = fopen( journal_path, "a+b" );
    if ( !journal ) {
        set_error( error, error_size, "Cannot open the registry journal" );
        return -1;
    }
    fseek( journal, 0L, SEEK_END );
    journal_start = ftell( journal );
    plan = fopen( plan_path, "r" );
    if ( !plan || plan_header( plan, journal_path, error, error_size ) ) {
        if ( plan ) fclose( plan );
        fclose( journal );
        return -1;
    }
    while ( !failed && fgets( reg_line, sizeof(reg_line), plan ) ) {
        if ( gdi_reg_parse_line( reg_line, &reg_entry, error, error_size ) )
            failed = 1;
        else if ( reg_entry.operation == GDI_REG_SET )
            failed = apply_set( &reg_entry, journal, error, error_size ) != 0;
        else
            failed = apply_delete( &reg_entry, journal, error, error_size ) != 0;
    }
    fclose( plan );
    fclose( journal );
    if ( failed ) {
        char original[160];
        FILE *truncate_file;
        strcpy( original, error );
        if ( restore_from( journal_path, journal_start, error, error_size ) ) {
            char rollback[160];
            strcpy( rollback, error );
            sprintf( error, "%.70s; rollback failed: %.60s", original, rollback );
            return -1;
        }
        truncate_file = fopen( journal_path, "r+b" );
        if ( !truncate_file || chsize( fileno( truncate_file ), journal_start ) ) {
            if ( truncate_file ) fclose( truncate_file );
            set_error( error, error_size,
                       "Registry rolled back, but its journal cannot be trimmed" );
            return -1;
        }
        fclose( truncate_file );
        if ( !journal_start )
            remove( journal_path );
        set_error( error, error_size, original );
        return -1;
    }
    return 0;
}
