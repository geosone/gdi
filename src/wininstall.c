/* wininstall.c - native Windows 3.1/9x frontend for GDI packages.
 *
 * This is the NE half of the dual-mode INSTALL.EXE. The complete DOS
 * installer is its MZ stub, so DOS runs the text installer while Windows
 * loads this frontend. The frontend extracts that stub temporarily when it
 * needs the existing transactional engine; only one installer is shipped.
 *
 * Copyright (C) 2026 GeosOne. GNU General Public License version 3. */

#include <windows.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <direct.h>
#include "gdi.h"
#include "regtxn.h"
#include "winreg16.h"
#include "wintasks.h"

#define MAXOPT       16
#define ID_OPTION    100
#define ID_INSTALL   200
#define ID_CANCEL    201

static HINSTANCE app_instance;
static HWND option_window[MAXOPT];
static HWND install_window;
static HWND cancel_window;
static HFONT ui_font;
static int option_count;
static int has_automatic;
static int win9x;
static int engine_running;
static char source_sidecar[128];
static int uninstall_mode;
static char source_dir[128];
static char module_path[128];
static char engine_path[128];
static char completion_path[128];
static char package_ini[128];
static char registry_plan_path[128];
static char task_plan_path[128];
static char setup_title[80];
static char product[100];
static char welcome_text[1000];
static char environment_text[80];
static char option_label[MAXOPT][80];
static char option_on[MAXOPT];

static void path_cat( char *out, const char *dir, const char *name )
{
    strcpy( out, dir );
    if ( *out && out[strlen( out ) - 1] != '\\' )
        strcat( out, "\\" );
    strcat( out, name );
}

static int file_exists( const char *path )
{
    OFSTRUCT of;
    HFILE file = OpenFile( path, &of, OF_EXIST );
    return file != HFILE_ERROR;
}

/* The Win16 UPX loader writes the original packed filename beside its
 * temporary executable as UPX*.SRC. */
static int packed_source_path( const char *module, char *path, unsigned path_size )
{
    char sidecar[128];
    HFILE file;
    unsigned i;
    long size;

    strcpy( sidecar, module );
    i = strlen( sidecar );
    if ( i < 3 )
        return 0;
    sidecar[i - 3] = 'S';
    sidecar[i - 2] = 'R';
    sidecar[i - 1] = 'C';
    file = _lopen( sidecar, OF_READ | OF_SHARE_DENY_NONE );
    if ( file == HFILE_ERROR )
        return 0;
    size = _llseek( file, 0, 2 );
    if ( size <= 0 || size >= (long)path_size || _llseek( file, 0, 0 ) < 0 ||
         _lread( file, path, (unsigned)size ) != (unsigned)size ) {
        _lclose( file );
        return 0;
    }
    path[size] = 0;
    _lclose( file );
    if ( !file_exists( path ) )
        return 0;
    file = _lcreat( sidecar, 0 );
    if ( file == HFILE_ERROR || _lwrite( file, "!", 1 ) != 1 ) {
        if ( file != HFILE_ERROR )
            _lclose( file );
        return 0;
    }
    _lclose( file );
    strcpy( source_sidecar, sidecar );
    return 1;
}

/* Return the first non-option command-line token as an explicit INI path. */
static int command_ini( const char FAR *command, char *path, unsigned path_size )
{
    unsigned length;
    int quoted;

    while ( *command ) {
        while ( *command == ' ' || *command == '\t' )
            command++;
        if ( !*command )
            break;
        quoted = *command == '"';
        if ( quoted )
            command++;
        length = 0;
        while ( *command && ( quoted ? *command != '"' :
                              *command != ' ' && *command != '\t' ) ) {
            if ( length + 1 < path_size )
                path[length++] = *command;
            command++;
        }
        path[length] = 0;
        if ( quoted && *command == '"' )
            command++;
        if ( length && path[0] != '/' && path[0] != '-' )
            return 1;
    }
    path[0] = 0;
    return 0;
}

static int delete_file( const char *path )
{
    OFSTRUCT of;
    OpenFile( path, &of, OF_DELETE );
    return !file_exists( path );
}

static void delete_uninstaller( void )
{
    char path[128], parent[128], *slash;
    path_cat( path, source_dir, "INSTALL.INI" );
    delete_file( path );
    path_cat( path, source_dir, "INSTALL.EXE" );
    delete_file( path );
    path_cat( path, source_dir, "GDIPOST.EXE" );
    gdi_tasks_run( path, 0 );
    delete_file( path );
    path_cat( path, source_dir, "UNINST.PND" );
    delete_file( path );
    strcpy( parent, source_dir );
    slash = strrchr( parent, '\\' );
    if ( slash == parent + 2 )
        slash[1] = 0;
    else if ( slash )
        *slash = 0;
    if ( slash )
        chdir( parent );
    rmdir( source_dir );
}

/* -1 = not complete yet; 0 = success; 1 = cancelled; 2 = failed. */
static int read_completion( void )
{
    OFSTRUCT of;
    HFILE file;
    char status;

    file = OpenFile( completion_path, &of, OF_READ | OF_SHARE_DENY_WRITE );
    if ( file == HFILE_ERROR )
        return -1;
    if ( _lread( file, &status, 1 ) != 1 )
        status = 0;
    _lclose( file );
    return status >= '0' && status <= '2' ? status - '0' : -1;
}

static void release_source_path( void )
{
    if ( *source_sidecar ) {
        delete_file( source_sidecar );
        source_sidecar[0] = 0;
    }
}

/* Write the MZ part before the NE header to a temporary executable. */
static int extract_dos_engine( void )
{
    OFSTRUCT of;
    HFILE input, output;
    BYTE header[64];
    static BYTE buffer[2048];
    LONG file_size, stub_size, left, header_size;
    UINT reloc_count, reloc_offset, reloc_bytes;
    UINT want, got, unique;
    char *dot;
    int attempt;

    input = OpenFile( module_path, &of, OF_READ | OF_SHARE_DENY_WRITE );
    if ( input == HFILE_ERROR )
        return 0;
    if ( _lread( input, header, sizeof(header) ) != sizeof(header) ||
         header[0] != 'M' || header[1] != 'Z' ) {
        _lclose( input );
        return 0;
    }
    stub_size = (LONG)header[0x3c] |
                ( (LONG)header[0x3d] << 8 ) |
                ( (LONG)header[0x3e] << 16 ) |
                ( (LONG)header[0x3f] << 24 );
    header_size = ( (UINT)header[8] | ( (UINT)header[9] << 8 ) ) * 16L;
    reloc_count = (UINT)header[6] | ( (UINT)header[7] << 8 );
    reloc_offset = (UINT)header[0x18] | ( (UINT)header[0x19] << 8 );
    if ( reloc_count > ( sizeof(buffer) - 0x20 ) / 4 ) {
        _lclose( input );
        return 0;
    }
    reloc_bytes = reloc_count * 4;
    file_size = _llseek( input, 0L, 2 );
    if ( stub_size < (LONG)sizeof(header) || stub_size >= file_size ||
         header_size < (LONG)sizeof(header) ||
         header_size > (LONG)sizeof(buffer) ||
         reloc_offset + reloc_bytes > (UINT)header_size ||
         0x20 + reloc_bytes > (UINT)header_size ||
         _llseek( input, 0L, 0 ) < 0 ||
         _lread( input, buffer, (UINT)header_size ) != (UINT)header_size ) {
        _lclose( input );
        return 0;
    }

    /* Wlink moves the DOS relocation table to 40h and stores the NE offset
     * at 3Ch. Restore an old-style DOS header so Windows runs the extracted
     * image as a DOS task rather than looking for an NE header after EOF. */
    memmove( buffer + 0x20, buffer + reloc_offset, reloc_bytes );
    buffer[0x18] = 0x20;
    buffer[0x19] = 0;

    engine_path[0] = 0;
    unique = (UINT)GetTickCount();
    if ( unique == 0 )
        unique = 1;
    for ( attempt = 0; attempt < 100; attempt++ ) {
        if ( !GetTempFileName( GetTempDrive( 0 ), "GDI", unique + attempt,
                               engine_path ) )
            continue;
        dot = strrchr( engine_path, '.' );
        if ( dot )
            strcpy( dot, ".EXE" );
        else
            strcat( engine_path, ".EXE" );
        strcpy( completion_path, engine_path );
        dot = strrchr( completion_path, '.' );
        if ( dot )
            strcpy( dot, ".STS" );
        else
            strcat( completion_path, ".STS" );
        strcpy( registry_plan_path, engine_path );
        dot = strrchr( registry_plan_path, '.' );
        if ( dot )
            strcpy( dot, ".REG" );
        else
            strcat( registry_plan_path, ".REG" );
        strcpy( task_plan_path, engine_path );
        dot = strrchr( task_plan_path, '.' );
        if ( dot )
            strcpy( dot, ".TSK" );
        else
            strcat( task_plan_path, ".TSK" );
        if ( !file_exists( engine_path ) && !file_exists( completion_path ) &&
             !file_exists( registry_plan_path ) && !file_exists( task_plan_path ) )
            break;
        engine_path[0] = 0;
        completion_path[0] = 0;
        registry_plan_path[0] = 0;
        task_plan_path[0] = 0;
    }
    if ( !*engine_path ) {
        _lclose( input );
        return 0;
    }
    output = _lcreat( engine_path, 0 );
    if ( output == HFILE_ERROR ) {
        _lclose( input );
        engine_path[0] = 0;
        completion_path[0] = 0;
        registry_plan_path[0] = 0;
        return 0;
    }
    if ( _lwrite( output, buffer, (UINT)header_size ) != (UINT)header_size ||
         _llseek( input, header_size, 0 ) < 0 )
        left = -1;
    else
        left = stub_size - header_size;
    while ( left > 0 ) {
        want = left > (LONG)sizeof(buffer) ? sizeof(buffer) : (UINT)left;
        got = _lread( input, buffer, want );
        if ( got != want || _lwrite( output, buffer, want ) != want )
            break;
        left -= want;
    }
    _lclose( output );
    _lclose( input );
    if ( left != 0 ) {
        delete_file( engine_path );
        engine_path[0] = 0;
        completion_path[0] = 0;
        registry_plan_path[0] = 0;
        return 0;
    }
    return 1;
}

static void read_package( void )
{
    const char *value, *bar, *bar2;
    int pos = 0, length;

    sprintf( setup_title, "%.79s", ini_get( "Setup", "Title", "Setup" ) );
    sprintf( product, "%.99s", ini_get( "Setup", "Product", "GDI package" ) );
    welcome_text[0] = 0;
    while ( ( pos = ini_text( "Welcome", pos, &value ) ) >= 0 &&
            strlen( welcome_text ) + strlen( value ) < sizeof(welcome_text) - 3 ) {
        if ( !*value )
            strcat( welcome_text, "\r\n\r\n" );
        else {
            if ( *welcome_text && welcome_text[strlen( welcome_text ) - 1] != '\n' )
                strcat( welcome_text, " " );
            strcat( welcome_text, value );
        }
    }
    if ( !*welcome_text )
        wsprintf( welcome_text, "This program installs %s.", (LPSTR)product );

    if ( uninstall_mode ) {
        sprintf( welcome_text, "%.999s",
                 ini_get( "Text", "UninstText",
                 "Remove the installation and restore its previous system settings?" ) );
        option_count = 0;
        has_automatic = 0;
        return;
    }

    option_count = 0;
    has_automatic = 0;
    pos = 0;
    while ( option_count < MAXOPT &&
            ( pos = ini_next( "Options", "Option", pos, &value ) ) >= 0 ) {
        bar = strchr( value, '|' );
        if ( ( *value == '?' || ( *value == '!' && value[1] == '?' ) ) && bar ) {
            option_on[option_count] = 2;  /* automatic: DOS engine detects */
            has_automatic = 1;
        } else
            option_on[option_count] = !strnicmp( value, "on", 2 ) ||
                                      *value == '1' || toupper( *value ) == 'Y';
        bar = bar ? bar + 1 : value;
        bar2 = strchr( bar, '|' );
        length = bar2 ? bar2 - bar : strlen( bar );
        if ( length > 79 )
            length = 79;
        sprintf( option_label[option_count], "%.*s", length, bar );
        option_count++;
    }
}

static int registry_preflight( char *message )
{
    const char *line;
    int pos = 0, number = 0;
    while ( ( pos = ini_lines( "Registry", pos, &line ) ) >= 0 ) {
        struct gdi_reg_entry entry;
        unsigned char data[GDI_REG_DATA_MAX];
        unsigned data_size;
        char error[100];
        number++;
        if ( gdi_reg_parse_line( line, &entry, error, sizeof(error) ) ||
             ( entry.operation == GDI_REG_SET && !strchr( entry.text, '%' ) &&
               gdi_reg_make_data( &entry, data, &data_size,
                                  error, sizeof(error) ) ) ) {
            wsprintf( message, "Registry line %d: %.90s", number,
                      (LPSTR)error );
            return 0;
        }
    }
    return 1;
}

static HWND make_control( HWND parent, const char *class_name, const char *text,
                          DWORD style, int x, int y, int width, int height, int id )
{
    HWND control = CreateWindow( class_name, text, WS_CHILD | WS_VISIBLE | style,
                                 x, y, width, height, parent, (HMENU)id,
                                 app_instance, NULL );
    if ( control && ui_font )
        SendMessage( control, WM_SETFONT, (WPARAM)ui_font, 0L );
    return control;
}

static void start_dos_setup( HWND window )
{
    char command[400], bits[MAXOPT + 1];
    int i;
    UINT result;
    if ( engine_running )
        return;
    if ( !extract_dos_engine() ) {
        MessageBox( window, "The embedded DOS installation engine could not be extracted.",
                    setup_title, MB_OK | MB_ICONEXCLAMATION );
        return;
    }
    for ( i = 0; i < option_count; i++ ) {
        int state = SendMessage( option_window[i], BM_GETCHECK, 0, 0L );
        bits[i] = state == 2 ? 'x' : state ? '1' : '0';
    }
    bits[option_count] = 0;
    if ( uninstall_mode )
        wsprintf( command, "%s /U /K /W:\"%s\" \"%s\"",
                  (LPSTR)engine_path, (LPSTR)completion_path,
                  (LPSTR)package_ini );
    else
        wsprintf( command,
                  "%s /O:%s /W:\"%s\" /R:\"%s\" /T:\"%s\" /F:%s \"%s\"",
                  (LPSTR)engine_path, (LPSTR)bits, (LPSTR)completion_path,
                  (LPSTR)registry_plan_path, (LPSTR)task_plan_path,
                  (LPSTR)( win9x ? "9X" : "31" ), (LPSTR)package_ini );
    result = WinExec( command, SW_SHOWNORMAL );
    if ( result <= 32 ) {
        char error[120];
        delete_file( engine_path );
        engine_path[0] = 0;
        completion_path[0] = 0;
        registry_plan_path[0] = 0;
        wsprintf( error,
                  "Windows could not start the DOS installation engine (error %u).",
                  result );
        MessageBox( window, error, setup_title, MB_OK | MB_ICONEXCLAMATION );
        return;
    }
    engine_running = 1;
    SetTimer( window, 1, 1000, NULL );
    ShowWindow( window, SW_MINIMIZE );
}

/* GDIPOST.EXE (run= in WIN.INI): the work a plain DOS installation left for
 * Windows - REGISTRY.PLN, or the restore after INSTALL /U (UNINST.PND) */
static int post_tasks( void )
{
    char path[160], post[160], error[180];
    DWORD version = GetVersion();
    int failed = 0, nine = LOBYTE( LOWORD( version ) ) >= 4 ||
        ( LOBYTE( LOWORD( version ) ) == 3 && HIBYTE( LOWORD( version ) ) >= 90 );
    path_cat( post, source_dir, "GDIPOST.EXE" );
    path_cat( path, source_dir, "REGISTRY.PLN" );
    if ( file_exists( path ) ) {
        if ( nine ) {
            strcpy( error, "Windows 9x registry backend is not available yet." );
            failed = 1;
        } else if ( !( failed = gdi_reg16_apply_plan( path, error, sizeof(error) ) ) )
            delete_file( path );
    }
    path_cat( path, source_dir, "UNINST.PND" );
    if ( !failed && file_exists( path ) ) {
        path_cat( path, source_dir, "REGISTRY.LOG" );
        if ( file_exists( path ) && !nine )
            failed = gdi_reg16_restore_journal( path, error, sizeof(error) );
        if ( !failed ) {
            delete_file( path );
            delete_uninstaller();
            return 0;
        }
    }
    if ( failed ) {
        MessageBox( NULL, error, "GDI", MB_OK | MB_ICONEXCLAMATION );
        return 2;
    }
    gdi_tasks_run( post, 0 );
    return 0;
}

static long FAR PASCAL window_proc( HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam )
{
    int i, rows, group_height, button_y, status;
    (void)lparam;
    switch ( message ) {
    case WM_CREATE:
        make_control( window, "STATIC", product, SS_LEFT,
                      18, 14, 540, 20, 0 );
        make_control( window, "STATIC", environment_text, SS_LEFT,
                      18, 36, 540, 18, 0 );
        make_control( window, "STATIC", welcome_text, SS_LEFT,
                      18, 62, 540, 60, 0 );
        if ( has_automatic )
            make_control( window, "STATIC",
                          ini_get( "Text", "WinAuto", "Three-state choices use automatic package detection." ),
                          SS_LEFT, 18, 124, 540, 18, 0 );
        rows = option_count > 8 ? 8 : option_count;
        group_height = rows * 22 + 30;
        make_control( window, "BUTTON", ini_get( "Text", "OptionsTitle", "Components" ),
                      BS_GROUPBOX, 12, 144, 552, group_height, 0 );
        for ( i = 0; i < option_count; i++ ) {
            int column = i / 8;
            int row = i % 8;
            option_window[i] = make_control( window, "BUTTON", option_label[i],
                                              ( option_on[i] == 2 ? BS_AUTO3STATE : BS_AUTOCHECKBOX ) |
                                              WS_TABSTOP,
                                              26 + column * 270, 166 + row * 22,
                                              option_count > 8 ? 258 : 520,
                                              20, ID_OPTION + i );
            SendMessage( option_window[i], BM_SETCHECK, option_on[i], 0L );
        }
        button_y = 154 + group_height;
        install_window = make_control( window, "BUTTON",
                      uninstall_mode ? ini_get( "Text", "Remove", "Remove" ) :
                      ini_get( "Text", "Install", "Start setup" ),
                      BS_DEFPUSHBUTTON | WS_TABSTOP, 350, button_y,
                      100, 26, ID_INSTALL );
        cancel_window = make_control( window, "BUTTON",
                      ini_get( "Text", "Cancel", "Cancel" ),
                      BS_PUSHBUTTON | WS_TABSTOP, 462, button_y,
                      90, 26, ID_CANCEL );
        return 0;
    case WM_COMMAND:
        if ( wparam == ID_INSTALL ) {
            start_dos_setup( window );
            return 0;
        }
        if ( wparam == ID_CANCEL ) {
            DestroyWindow( window );
            return 0;
        }
        break;
    case WM_TIMER:
        /* Remove the extracted image once Windows releases it, but keep this
         * task alive until the DOS engine reports its final status. */
        if ( engine_running && *engine_path && delete_file( engine_path ) )
            engine_path[0] = 0;
        status = engine_running ? read_completion() : -1;
        if ( status >= 0 ) {
            char registry_error[180];
            char journal_path[128];
            engine_running = 0;
            delete_file( completion_path );
            completion_path[0] = 0;
            if ( status == 0 ) {
                int registry_failed = 0;
                if ( uninstall_mode ) {
                    path_cat( journal_path, source_dir, "REGISTRY.LOG" );
                    /* WIN.INI values and groups, then the registry */
                    registry_failed = gdi_tasks_restore( source_dir,
                        registry_error, sizeof(registry_error) );
                    if ( !registry_failed && file_exists( journal_path ) ) {
                        if ( win9x ) {
                            strcpy( registry_error,
                                    "Windows 9x registry restore is not available yet." );
                            registry_failed = 1;
                        } else
                            registry_failed = gdi_reg16_restore_journal(
                                journal_path, registry_error,
                                sizeof(registry_error) );
                        if ( !registry_failed ) {
                            delete_file( journal_path );
                            delete_uninstaller();
                        }
                    }
                    else if ( !registry_failed )
                        delete_uninstaller();
                } else {
                    if ( !win9x && *registry_plan_path )
                        registry_failed = gdi_reg16_apply_plan(
                            registry_plan_path, registry_error,
                            sizeof(registry_error) );
                    if ( !registry_failed && *task_plan_path )
                        registry_failed = gdi_tasks_apply( task_plan_path,
                            registry_error, sizeof(registry_error) );
                }
                if ( registry_failed )
                    MessageBox( window, registry_error, setup_title,
                                MB_OK | MB_ICONEXCLAMATION );
            }
            if ( *registry_plan_path )
                delete_file( registry_plan_path );
            registry_plan_path[0] = 0;
            if ( *task_plan_path )
                delete_file( task_plan_path );
            task_plan_path[0] = 0;
            KillTimer( window, 1 );
            DestroyWindow( window );
        }
        return 0;
    case WM_DESTROY:
        if ( *engine_path )
            delete_file( engine_path );
        if ( *completion_path )
            delete_file( completion_path );
        if ( *registry_plan_path )
            delete_file( registry_plan_path );
        if ( *task_plan_path )
            delete_file( task_plan_path );
        release_source_path();
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProc( window, message, wparam, lparam );
}

int PASCAL WinMain( HINSTANCE instance, HINSTANCE previous,
                    LPSTR command_line, int show )
{
    WNDCLASS window_class;
    MSG message;
    HWND window;
    DWORD version;
    unsigned major, minor;
    char module[128], registry_error[130], *slash;
    int rows, height, x, y;

    {
        LPSTR p = command_line;
        while ( *p ) {
            while ( *p == ' ' || *p == '\t' ) p++;
            if ( ( *p == '/' || *p == '-' ) && toupper( p[1] ) == 'U' &&
                 ( !p[2] || p[2] == ' ' || p[2] == '\t' ) )
                uninstall_mode = 1;
            while ( *p && *p != ' ' && *p != '\t' ) p++;
        }
    }
    app_instance = instance;
    GetModuleFileName( instance, module, sizeof(module) );
    strcpy( module_path, module );
    if ( !packed_source_path( module, source_dir, sizeof(source_dir) ) )
        strcpy( source_dir, module );
    slash = strrchr( source_dir, '\\' );
    if ( slash && !stricmp( slash + 1, "GDIPOST.EXE" ) ) {
        int rc;
        *slash = 0;
        rc = post_tasks();
        release_source_path();
        return rc;
    }
    if ( slash )
        *slash = 0;
    else
        strcpy( source_dir, "." );
    if ( command_ini( command_line, package_ini, sizeof(package_ini) ) ) {
        strcpy( source_dir, package_ini );
        slash = strrchr( source_dir, '\\' );
        if ( slash )
            *slash = 0;
        else
            strcpy( source_dir, "." );
    } else
        path_cat( package_ini, source_dir, "INSTALL.INI" );
    if ( ini_load( package_ini ) ) {
        MessageBox( NULL, "INSTALL.INI was not found next to INSTALL.EXE.",
                    "GDI", MB_OK | MB_ICONEXCLAMATION );
        release_source_path();
        return 2;
    }

    if ( !stricmp( ini_get( "Setup", "Windows", "yes" ), "no" ) ) {
        MessageBox( NULL, ini_get( "Text", "NeedDos",
                    "Please quit Windows and run this setup from MS-DOS." ),
                    ini_get( "Setup", "Title", "Setup" ), MB_OK | MB_ICONINFORMATION );
        ini_free();
        release_source_path();
        return 1;
    }
    version = GetVersion();
    major = LOBYTE( LOWORD( version ) );
    minor = HIBYTE( LOWORD( version ) );
    win9x = major >= 4 || ( major == 3 && minor >= 90 );
    wsprintf( environment_text, "Native Win16 setup - Windows %u.%02u (%s)",
              major, minor,
              (LPSTR)( win9x ? "Windows 9x/ME" : "Windows 3.x" ) );
    read_package();
    if ( !registry_preflight( registry_error ) ) {
        MessageBox( NULL, registry_error, setup_title,
                    MB_OK | MB_ICONEXCLAMATION );
        ini_free();
        release_source_path();
        return 2;
    }

    if ( !previous ) {
        memset( &window_class, 0, sizeof(window_class) );
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hIcon = LoadIcon( NULL, IDI_APPLICATION );
        window_class.hCursor = LoadCursor( NULL, IDC_ARROW );
        window_class.hbrBackground = (HBRUSH)( COLOR_BTNFACE + 1 );
        window_class.lpszClassName = "GDIWinInstall";
        if ( !RegisterClass( &window_class ) ) {
            ini_free();
            release_source_path();
            return 2;
        }
    }
    ui_font = GetStockObject( SYSTEM_FONT );
    rows = option_count > 8 ? 8 : option_count;
    height = 235 + rows * 22;
    x = ( GetSystemMetrics( SM_CXSCREEN ) - 580 ) / 2;
    y = ( GetSystemMetrics( SM_CYSCREEN ) - height ) / 2;
    window = CreateWindow( "GDIWinInstall", setup_title,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           x, y, 580, height, NULL, NULL, instance, NULL );
    if ( !window ) {
        ini_free();
        release_source_path();
        return 2;
    }
    ShowWindow( window, show );
    UpdateWindow( window );
    SetFocus( install_window );
    while ( GetMessage( &message, NULL, 0, 0 ) ) {
        if ( message.message == WM_KEYDOWN ) {
            if ( message.wParam == VK_RETURN ) {
                SendMessage( window, WM_COMMAND,
                             GetFocus() == cancel_window ? ID_CANCEL : ID_INSTALL,
                             0L );
                continue;
            }
            if ( message.wParam == VK_ESCAPE ) {
                SendMessage( window, WM_COMMAND, ID_CANCEL, 0L );
                continue;
            }
            if ( message.wParam == VK_TAB ) {
                HWND next = GetNextDlgTabItem( window, GetFocus(),
                                               GetKeyState( VK_SHIFT ) < 0 );
                if ( next )
                    SetFocus( next );
                continue;
            }
        }
        TranslateMessage( &message );
        DispatchMessage( &message );
    }
    ini_free();
    return message.wParam;
}
