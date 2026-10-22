/* ui_win16.c - the user interface of the installer core in Windows 3.x and
 * 9x: windows with buttons, a line of input, a text viewer, a list of
 * check boxes and a progress window.  The texts come in the DOS code page
 * (INSTALL.INI, file names) and are shown in the Windows code page.
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <windows.h>
#include <string.h>
#include <dos.h>
#include "gdi.h"
#include "ui.h"

#define ID_BUTTON   100   /* + number of the button */
#define ID_EDIT     200
#define ID_LIST     201
#define ID_TEXT     202
#define MAXBUTTON   8
#define WIDTH       460   /* client area of a window */
#define MARGIN      12
#define TEXTMAX     280   /* a longer text gets a scroll bar */

HINSTANCE ui_instance;   /* set by the frontend */

static HFONT font;
static int char_height, char_width;
static int registered;
static int result, done, default_button, error_icon;
static HWND progress_window, progress_text;
static long progress_done, progress_total;
static char conv[2048];
static char button_key[MAXBUTTON];   /* first letter of each button: a key */
static int buttons;
static HBRUSH face;

/* a text of the core in the Windows code page, '\n' as CR LF */
static const char *ansi( const char *text )
{
    unsigned n = 0;
    for ( ; *text && n < sizeof(conv) - 3; text++ ) {
        if ( *text == '\n' && ( n == 0 || conv[n - 1] != '\r' ) )
            conv[n++] = '\r';
        conv[n++] = *text;
    }
    conv[n] = 0;
    OemToAnsi( conv, conv );
    return conv;
}

static void pump( void )
{
    MSG message;
    while ( PeekMessage( &message, NULL, 0, 0, PM_REMOVE ) ) {
        TranslateMessage( &message );
        DispatchMessage( &message );
    }
}

LRESULT CALLBACK ui_proc( HWND window, UINT message, WPARAM wparam, LPARAM lparam )
{
    switch ( message ) {
    case WM_COMMAND:
        if ( wparam >= ID_BUTTON && wparam < ID_BUTTON + MAXBUTTON ) {
            result = wparam - ID_BUTTON;
            done = 1;
        } else if ( wparam == IDOK ) {
            result = default_button;
            done = 1;
        } else if ( wparam == IDCANCEL ) {
            result = -1;
            done = 1;
        }
        return 0;
    case WM_CLOSE:
        if ( window == progress_window )
            return 0;   /* only the core ends it */
        result = -1;
        done = 1;
        return 0;
    case WM_CTLCOLOR:
        if ( HIWORD( lparam ) == CTLCOLOR_STATIC || HIWORD( lparam ) == CTLCOLOR_BTN ) {
            SetBkColor( (HDC)wparam, GetSysColor( COLOR_BTNFACE ) );
            return (LRESULT)face;
        }
        break;
    case WM_MEASUREITEM:
        ( (MEASUREITEMSTRUCT FAR *)lparam )->itemHeight = char_height + 4;
        return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT FAR *item = (DRAWITEMSTRUCT FAR *)lparam;
        char text[100];
        RECT box;
        int size = char_height - 2, y;
        if ( item->itemID == (UINT)-1 )
            return TRUE;
        if ( item->itemAction & ( ODA_DRAWENTIRE | ODA_SELECT ) ) {
            FillRect( item->hDC, &item->rcItem, GetStockObject( WHITE_BRUSH ) );
            y = item->rcItem.top + ( item->rcItem.bottom - item->rcItem.top - size ) / 2;
            SetRect( &box, item->rcItem.left + 4, y, item->rcItem.left + 4 + size, y + size );
            FrameRect( item->hDC, &box, GetStockObject( BLACK_BRUSH ) );
            if ( item->itemState & ODS_SELECTED ) {   /* selected = switched on */
                MoveTo( item->hDC, box.left + 2, box.top + 2 );
                LineTo( item->hDC, box.right - 2, box.bottom - 2 );
                MoveTo( item->hDC, box.right - 3, box.top + 2 );
                LineTo( item->hDC, box.left + 1, box.bottom - 2 );
            }
            SendMessage( item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)(LPSTR)text );
            SetBkMode( item->hDC, TRANSPARENT );
            SetTextColor( item->hDC, GetSysColor( COLOR_WINDOWTEXT ) );
            TextOut( item->hDC, box.right + 6, item->rcItem.top + 2, text, strlen( text ) );
        }
        if ( item->itemAction & ODA_FOCUS || ( item->itemState & ODS_FOCUS ) )
            DrawFocusRect( item->hDC, &item->rcItem );
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint( window, &paint );
        if ( window == progress_window ) {
            RECT bar;
            HBRUSH brush = CreateSolidBrush( GetSysColor( COLOR_ACTIVECAPTION ) );
            SetRect( &bar, MARGIN, MARGIN + 2 * char_height, WIDTH - MARGIN,
                     MARGIN + 2 * char_height + char_height + 6 );
            FrameRect( dc, &bar, GetStockObject( BLACK_BRUSH ) );
            InflateRect( &bar, -1, -1 );
            if ( progress_total > 0 ) {
                bar.right = bar.left + (int)( ( bar.right - bar.left ) *
                    ( progress_done > progress_total ? progress_total : progress_done ) / progress_total );
                FillRect( dc, &bar, brush );
            }
            DeleteObject( brush );
        } else if ( error_icon )
            DrawIcon( dc, MARGIN, MARGIN, LoadIcon( NULL, IDI_EXCLAMATION ) );
        EndPaint( window, &paint );
        return 0;
    }
    }
    return DefWindowProc( window, message, wparam, lparam );
}

void ui_init( void )
{
    if ( !registered ) {
        WNDCLASS c;
        HDC dc;
        TEXTMETRIC metrics;
        memset( &c, 0, sizeof(c) );
        c.lpfnWndProc = ui_proc;
        c.hInstance = ui_instance;
        c.hIcon = LoadIcon( NULL, IDI_APPLICATION );
        c.hCursor = LoadCursor( NULL, IDC_ARROW );
        c.hbrBackground = (HBRUSH)( COLOR_BTNFACE + 1 );
        c.lpszClassName = "GDIWindow";
        RegisterClass( &c );
        registered = 1;
        font = GetStockObject( SYSTEM_FONT );
        dc = GetDC( NULL );
        SelectObject( dc, font );
        GetTextMetrics( dc, &metrics );
        ReleaseDC( NULL, dc );
        char_height = metrics.tmHeight + metrics.tmExternalLeading;
        char_width = metrics.tmAveCharWidth;
        face = CreateSolidBrush( GetSysColor( COLOR_BTNFACE ) );
    }
}

void ui_exit( void )
{
    ui_clear();
}

void ui_title( const char *title, const char *product )
{
    (void)title;
    (void)product;
}

void ui_clear( void )
{
    if ( progress_window ) {
        DestroyWindow( progress_window );
        progress_window = NULL;
    }
}

/* a window with the client area width x height in the middle of the screen */
static HWND make_window( const char *title, int height )
{
    RECT r;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_BORDER;
    SetRect( &r, 0, 0, WIDTH, height );
    AdjustWindowRect( &r, style, FALSE );
    return CreateWindow( "GDIWindow", ansi( title ), style,
                         ( GetSystemMetrics( SM_CXSCREEN ) - ( r.right - r.left ) ) / 2,
                         ( GetSystemMetrics( SM_CYSCREEN ) - ( r.bottom - r.top ) ) / 2,
                         r.right - r.left, r.bottom - r.top, NULL, NULL, ui_instance, NULL );
}

static HWND control( HWND parent, const char *class_name, const char *text, DWORD style,
                     int x, int y, int width, int height, int id, HINSTANCE instance )
{
    HWND c = CreateWindow( class_name, text, WS_CHILD | WS_VISIBLE | style, x, y,
                           width, height, parent, (HMENU)id,
                           instance ? instance : ui_instance, NULL );
    SendMessage( c, WM_SETFONT, (WPARAM)font, 0L );
    return c;
}

/* height of a text in the width of a window */
static int text_height( const char *text, int width )
{
    RECT r;
    HDC dc = GetDC( NULL );
    SelectObject( dc, font );
    SetRect( &r, 0, 0, width, 0 );
    DrawText( dc, text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX );
    ReleaseDC( NULL, dc );
    return r.bottom;
}

/* the text of a window: static, or with a scroll bar when it is long */
static int text_control( HWND window, const char *text, int x, int y, int width )
{
    int height = text_height( ansi( text ), width );
    if ( height > TEXTMAX ) {
        control( window, "EDIT", conv, ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER,
                 x, y, width, TEXTMAX, ID_TEXT, NULL );
        return TEXTMAX;
    }
    control( window, "STATIC", conv, SS_LEFT | SS_NOPREFIX, x, y, width, height, ID_TEXT, NULL );
    return height;
}

/* buttons right-aligned below y, in more rows when they do not fit;
 * returns their height */
static int button_layout( HWND window, const char **labels, int n, int y, int sel, int create )
{
    int widths[MAXBUTTON], i, x, rows = 1, height = char_height + 10;
    HDC dc = GetDC( NULL );
    SelectObject( dc, font );
    for ( i = 0; i < n; i++ ) {
        DWORD extent;
        ansi( labels[i] );
        extent = GetTextExtent( dc, conv, strlen( conv ) );
        widths[i] = LOWORD( extent ) + 24 > 90 ? LOWORD( extent ) + 24 : 90;
    }
    ReleaseDC( NULL, dc );
    x = WIDTH - MARGIN;
    if ( create ) {
        buttons = n;
        for ( i = 0; i < n; i++ )
            button_key[i] = (char)AnsiUpper( (LPSTR)(DWORD)(BYTE)*ansi( labels[i] ) );
    }
    for ( i = n - 1; i >= 0; i-- ) {
        if ( x - widths[i] < MARGIN ) {
            x = WIDTH - MARGIN;
            rows++;
        }
        x -= widths[i];
        if ( create )
            control( window, "BUTTON", ansi( labels[i] ),
                     ( i == sel ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON ) | WS_TABSTOP,
                     x, y + ( rows - 1 ) * ( height + 6 ), widths[i], height,
                     ID_BUTTON + i, NULL );
        x -= 8;
    }
    return rows * ( height + 6 ) - 6;
}

static int run( HWND window, HWND focus )
{
    MSG message;
    ShowWindow( window, SW_SHOW );
    UpdateWindow( window );
    SetFocus( focus );
    done = 0;
    result = -1;
    while ( !done && GetMessage( &message, NULL, 0, 0 ) ) {
        char class_name[8];
        int i;
        /* the first letter of a button chooses it, as in the text mode */
        if ( message.message == WM_CHAR && GetFocus() &&
             GetClassName( GetFocus(), class_name, sizeof(class_name) ) &&
             !stricmp( class_name, "Button" ) ) {
            char key = (char)AnsiUpper( (LPSTR)(DWORD)(BYTE)message.wParam );
            for ( i = 0; i < buttons && button_key[i] != key; i++ )
                ;
            if ( i < buttons ) {
                result = i;
                done = 1;
                continue;
            }
        }
        if ( !IsDialogMessage( window, &message ) ) {
            TranslateMessage( &message );
            DispatchMessage( &message );
        }
    }
    buttons = 0;
    error_icon = 0;
    return result;
}

int ui_message( const char *title, const char *text, const char **buttons, int n, int sel, int error )
{
    HWND window;
    int x = error ? MARGIN + 32 + MARGIN : MARGIN, height, bh;
    if ( n > MAXBUTTON )
        n = MAXBUTTON;
    height = text_height( ansi( text ), WIDTH - x - MARGIN );
    if ( height > TEXTMAX )
        height = TEXTMAX;
    if ( error && height < 32 )
        height = 32;
    bh = button_layout( NULL, buttons, n, 0, sel, 0 );
    error_icon = error;
    if ( error )
        MessageBeep( MB_ICONEXCLAMATION );
    default_button = sel;
    window = make_window( title, MARGIN + height + MARGIN + bh + MARGIN );
    text_control( window, text, x, MARGIN, WIDTH - x - MARGIN );
    button_layout( window, buttons, n, MARGIN + height + MARGIN, sel, 1 );
    n = run( window, GetDlgItem( window, ID_BUTTON + sel ) );
    DestroyWindow( window );
    return n;
}

int ui_input( const char *title, const char *text, char *buf, int max )
{
    static const char *b[2];
    HWND window, edit;
    int height, y, bh, rc;
    b[0] = gdi_text( "OK" );
    b[1] = gdi_text( "Cancel" );
    height = text_height( ansi( text ), WIDTH - 2 * MARGIN );
    if ( height > TEXTMAX )
        height = TEXTMAX;
    bh = button_layout( NULL, b, 2, 0, 0, 0 );
    y = MARGIN + height + MARGIN;
    default_button = 0;
    window = make_window( title, y + char_height + 8 + MARGIN + bh + MARGIN );
    text_control( window, text, MARGIN, MARGIN, WIDTH - 2 * MARGIN );
    edit = control( window, "EDIT", ansi( buf ), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                    MARGIN, y, WIDTH - 2 * MARGIN, char_height + 8, ID_EDIT, NULL );
    SendMessage( edit, EM_LIMITTEXT, max, 0L );
    SendMessage( edit, EM_SETSEL, 0, MAKELPARAM( 0, -1 ) );
    button_layout( window, b, 2, y + char_height + 8 + MARGIN, 0, 1 );
    rc = run( window, edit );
    if ( rc == 0 ) {
        GetWindowText( edit, conv, max + 1 );
        AnsiToOem( conv, conv );
        strcpy( buf, conv );
    }
    DestroyWindow( window );
    return rc == 0 ? 0 : -1;
}

int ui_view( const char *title, ftext *lines, int nlines, const char **buttons, int n, int sel )
{
    HWND window, edit;
    HGLOBAL text, heap;
    char __far *p, __far *base;
    void __far *h;
    unsigned long size = 1;
    int i, bh, height = 18 * char_height, rc;
    for ( i = 0; i < nlines; i++ )
        size += _fstrlen( lines[i] ) + 2;
    if ( size > 0xF000 )
        size = 0xF000;
    text = GlobalAlloc( GMEM_MOVEABLE, size );
    /* the edit control keeps its text in a segment of its own, not in the
     * 64 KB of the installer's data */
    heap = GlobalAlloc( GMEM_MOVEABLE | GMEM_ZEROINIT, size + 0x1000 );
    if ( !text || !heap ) {
        if ( text )
            GlobalFree( text );
        if ( heap )
            GlobalFree( heap );
        return ui_message( title, "", buttons, n, sel, 0 );
    }
    base = p = GlobalLock( text );
    for ( i = 0; i < nlines; i++ ) {
        unsigned len = _fstrlen( lines[i] );
        if ( (unsigned long)( p - base ) + len + 3 > size )
            break;
        _fmemcpy( p, lines[i], len );
        p[len] = '\r';
        p[len + 1] = '\n';
        p += len + 2;
    }
    *p = 0;
    OemToAnsi( base, base );
    h = GlobalLock( heap );
    LocalInit( FP_SEG( h ), 0, (UINT)( GlobalSize( heap ) - 16 ) );
    UnlockSegment( FP_SEG( h ) );
    bh = button_layout( NULL, buttons, n, 0, sel, 0 );
    default_button = sel;
    window = make_window( title, MARGIN + height + MARGIN + bh + MARGIN );
    edit = control( window, "EDIT", "", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                    MARGIN, MARGIN, WIDTH - 2 * MARGIN, height, ID_TEXT,
                    (HINSTANCE)FP_SEG( h ) );
    SendMessage( edit, EM_LIMITTEXT, 0xF000, 0L );
    SetWindowText( edit, base );
    GlobalUnlock( text );
    GlobalFree( text );
    button_layout( window, buttons, n, MARGIN + height + MARGIN, sel, 1 );
    rc = run( window, GetDlgItem( window, ID_BUTTON + sel ) );
    DestroyWindow( window );
    GlobalUnlock( heap );
    GlobalFree( heap );
    return rc;
}

int ui_checklist( const char *title, const char *text, const char **items, char *on, int n )
{
    static const char *b[2];
    HWND window, list;
    int height, rows = n < 14 ? n : 14, y, bh, i, rc;
    b[0] = gdi_text( "OK" );
    b[1] = gdi_text( "Cancel" );
    height = text_height( ansi( text ), WIDTH - 2 * MARGIN );
    if ( height > TEXTMAX / 2 )
        height = TEXTMAX / 2;
    bh = button_layout( NULL, b, 2, 0, 0, 0 );
    y = MARGIN + height + MARGIN;
    default_button = 0;
    window = make_window( title, y + rows * ( char_height + 4 ) + 2 + MARGIN + bh + MARGIN );
    text_control( window, text, MARGIN, MARGIN, WIDTH - 2 * MARGIN );
    list = control( window, "LISTBOX", "", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                    LBS_MULTIPLESEL | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                    MARGIN, y, WIDTH - 2 * MARGIN, rows * ( char_height + 4 ) + 2, ID_LIST, NULL );
    for ( i = 0; i < n; i++ ) {
        SendMessage( list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)ansi( items[i] ) );
        SendMessage( list, LB_SETSEL, on[i] ? TRUE : FALSE, MAKELPARAM( i, 0 ) );
    }
    SendMessage( list, LB_SETCARETINDEX, 0, 0L );
    button_layout( window, b, 2, y + rows * ( char_height + 4 ) + 2 + MARGIN, 0, 1 );
    rc = run( window, list );
    if ( rc == 0 )
        for ( i = 0; i < n; i++ )
            on[i] = SendMessage( list, LB_GETSEL, i, 0L ) > 0;
    DestroyWindow( window );
    return rc == 0 ? 0 : -1;
}

void ui_progress( const char *title, const char *text, long done_, long total )
{
    if ( !progress_window ) {
        progress_window = make_window( title, MARGIN + 3 * char_height + 6 + MARGIN );
        progress_text = control( progress_window, "STATIC", "", SS_LEFT | SS_NOPREFIX,
                                 MARGIN, MARGIN, WIDTH - 2 * MARGIN, char_height, ID_TEXT, NULL );
        ShowWindow( progress_window, SW_SHOW );
    }
    SetWindowText( progress_text, ansi( text ) );
    progress_done = done_;
    progress_total = total;
    InvalidateRect( progress_window, NULL, FALSE );
    UpdateWindow( progress_window );
    pump();
}
