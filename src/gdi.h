/* gdi.h - GeosOne Dual Installer
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#define GDI_VERSION "1.9.0"

/* a line of a text file and the array of lines, both on the far heap */
typedef char __far *fline;
typedef fline __far ftext;

/* screen.c */
void scr_init( void );
void scr_exit( void );
void scr_cursor( int x, int y );
int  scr_key( void );
void scr_put( int x, int y, int attr, const char *s );
void scr_putn( int x, int y, int attr, const char *s, int n );
void scr_putnf( int x, int y, int attr, const char __far *s, int n );
void scr_fill( int x, int y, int w, int h, int ch, int attr );
void scr_window( int x, int y, int w, int h, int attr, const char *title );
void scr_bars( const char *left, const char *right, const char *keys );
void scr_keys( const char *keys );
void scr_desktop( void );
int  scr_text( int x, int y, int w, int h, int attr, const char *s, int draw );
int  scr_buttons( int y, int x, int w, const char **labels, int n, int sel );
int  scr_message( const char *title, const char *text, const char **buttons, int n, int sel, int error );
int  scr_input( const char *title, const char *text, char *buf, int max );
int  scr_view( const char *title, ftext *lines, int nlines, const char **buttons, int n, int sel );
int  scr_checklist( const char *title, const char *text, const char **items, char *on, int n );
void scr_progress( const char *title, const char *text, long done, long total );
void itoa_dec( int v, char *s );

/* ini.c */
int         ini_load( const char *path );
void        ini_free( void );
void        ini_save( void );
void        ini_restore( void );
const char *ini_get( const char *section, const char *key, const char *def );
int         ini_next( const char *section, const char *key, int pos, const char **value );
int         ini_lines( const char *section, int pos, const char **line );
int         ini_text( const char *section, int pos, const char **line );
void        ini_package_id( char *out, unsigned size );
/* text files: the lines are kept on the far heap (a long license must not
 * fill the 64 KB data segment); NULL = file not found or not enough memory */
ftext      *text_load( const char *path, int *nlines );
void        text_free( ftext *lines, int nlines );
/* DOS engine: no memory left (str_dup); does not return */
void        gdi_out_of_memory( void );
char       *str_dup( const char *s );

/* archive.c */
const char *arc_install( const char *base, int (*target)( const char *name, char *path ) );
const char *arc_errfile( void );

/* util.c (DOS engine and Windows frontend) */
void path_cat( char *out, const char *dir, const char *name );
int  file_exists( const char *path );   /* a file or a directory */
void strip_eol( char *line );
void set_error( char *error, unsigned size, const char *text );

/* journal.c (DOS engine and Windows frontend): GDI.INI on the boot drive,
 * DIRS.LOG */
int  gdi_record_read( const char *boot, const char *key, char *dir, char *bits, char *version );
void gdi_record_write( const char *boot, const char *key, const char *value );
void gdi_note_parents( const char *dir, char *top );
void gdi_remove_dirs( const char *dir );

/* install.c: called by archive.c */
void gdi_progress( unsigned long done, unsigned long total );
void gdi_progress_name( const char *name );
int  gdi_ask_disk( int n );
