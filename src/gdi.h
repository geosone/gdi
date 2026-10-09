/* gdi.h - GeosOne DOS Installer
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#define GDI_VERSION "1.0"

/* screen.c */
void scr_init( void );
void scr_exit( void );
void scr_cursor( int x, int y );
int  scr_key( void );
void scr_put( int x, int y, int attr, const char *s );
void scr_putn( int x, int y, int attr, const char *s, int n );
void scr_fill( int x, int y, int w, int h, int ch, int attr );
void scr_window( int x, int y, int w, int h, int attr, const char *title );
void scr_bars( const char *left, const char *right, const char *keys );
void scr_keys( const char *keys );
void scr_desktop( void );
int  scr_text( int x, int y, int w, int h, int attr, const char *s, int draw );
int  scr_buttons( int y, int x, int w, const char **labels, int n, int sel );
int  scr_message( const char *title, const char *text, const char **buttons, int n, int sel, int error );
int  scr_input( const char *title, const char *text, char *buf, int max );
int  scr_view( const char *title, char **lines, int nlines, const char **buttons, int n, int sel );
int  scr_checklist( const char *title, const char *text, const char **items, char *on, int n );
void scr_progress( const char *title, const char *text, long done, long total );
void itoa_dec( int v, char *s );

/* ini.c */
int         ini_load( const char *path );
const char *ini_get( const char *section, const char *key, const char *def );
int         ini_next( const char *section, const char *key, int pos, const char **value );
int         ini_lines( const char *section, int pos, const char **line );
char      **text_load( const char *path, int *nlines );
char       *str_dup( const char *s );
