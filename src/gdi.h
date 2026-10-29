/* gdi.h - GeosOne Dual Installer
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#define GDI_VERSION "1.16.0"

/* a line of a text file and the array of lines, both on the far heap */
typedef char __far *fline;
typedef fline __far ftext;

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
#ifdef __WINDOWS__
extern int  ini_keep_oem;   /* do not convert INSTALL.INI to ANSI when loading */
#endif
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

/* files.c: called by archive.c */
void gdi_progress( unsigned long done, unsigned long total );
void gdi_progress_name( const char *name );
int  gdi_ask_disk( int n );
