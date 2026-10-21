/* wintasks.h - WIN.INI values and Program Manager items (Win16 frontend) */

#ifndef GDI_WINTASKS_H
#define GDI_WINTASKS_H

/* apply the plan of the DOS engine (/T:); 0 = done, else error is set */
int gdi_tasks_apply( const char *plan_path, char *error, unsigned size );
/* INSTALL /U: restore WIN.SET and PROGMAN.LOG of the installation dir */
int gdi_tasks_restore( const char *dir, char *error, unsigned size );
/* add (1) or remove (0) a program in run= of [windows] in WIN.INI */
void gdi_tasks_run( const char *program, int add );
/* starts a program of [Run] Windows= */
void gdi_tasks_start( const char *command );

#endif
