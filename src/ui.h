/* ui.h - the user interface of the GeosOne Dual Installer as the installer
 * core sees it.  One implementation per front end: ui_dos.c (text mode).
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

void ui_init( void );
void ui_exit( void );
/* the name of the setup and of the product, shown while it runs */
void ui_title( const char *title, const char *product );
/* an empty screen between two steps */
void ui_clear( void );
/* a message with buttons; returns the button chosen or -1 (Esc) */
int  ui_message( const char *title, const char *text, const char **buttons, int n, int sel, int error );
/* a line of input in buf; returns 0 = accepted, -1 = cancelled */
int  ui_input( const char *title, const char *text, char *buf, int max );
/* a long text (license, readme); returns the button chosen or -1 */
int  ui_view( const char *title, ftext *lines, int nlines, const char **buttons, int n, int sel );
/* options on[] switched by the user; returns 0 = accepted, -1 = cancelled */
int  ui_checklist( const char *title, const char *text, const char **items, char *on, int n );
/* progress: called with done = 0 first */
void ui_progress( const char *title, const char *text, long done, long total );

/* lang.c: a text of the installer: [Text] of INSTALL.INI, else the text of
 * the language of the system (src/language.ini), else the English one */
const char *gdi_text( const char *key );
/* the section of language.ini, by name (Setup /L:, [Setup] Texts=) */
int  gdi_text_language( const char *name );
/* the name of the section in use */
const char *gdi_text_name( void );
/* forget the texts in use (the Windows frontend changes the code page) */
void gdi_text_reset( void );
