/* winnative.c - GeosOne Dual Installer: the installer core runs in the
 * Windows frontend itself, with the windows of ui_win16.c.  Only what
 * Windows cannot do stays with the DOS engine (wininstall.c decides).
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "gdi.h"
#include "ui.h"
#include "hexpatch.h"
#include "core.h"

extern HINSTANCE ui_instance;

/* like the DOS engine started with /R /T /F (or /U /K): the registry, WIN.INI
 * and the groups are left in the plans for the frontend; returns 0, or
 * does not return (fail(), abort_setup()) */
int gdi_native_run( HINSTANCE instance, const char *ini, const char *dir, int remove,
                    int nine, const char *registry_plan, const char *task_plan )
{
    char error[130];
    ui_instance = instance;
    ini_free();
    ini_keep_oem = 1;
    gdi_text_reset();   /* the texts in the DOS code page now */
    if ( ini_load( ini ) )
        return 2;
    sprintf( srcdir, "%.79s", dir );
    in_windows = 1;
    uninstall = keep_uninstaller = remove;
    if ( !remove ) {
        sprintf( registry_plan_file, "%.79s", registry_plan );
        sprintf( task_plan_file, "%.79s", task_plan );
        registry_family = nine ? 9 : 3;
        if ( registry_preflight( error ) ) {
            ui_init();
            fail( error );
        }
    }
    return core_run();
}
