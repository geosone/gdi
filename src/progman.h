/* progman.h - Windows 3.x Program Manager groups (.GRP files) for GDI
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#ifndef GDI_PROGMAN_H
#define GDI_PROGMAN_H

#define GDI_PM_PATH 128

/* All strings are ANSI (the code page of Windows).  windir is the Windows
 * directory that holds PROGMAN.INI and the group files. */

/* Add an item to a group, creating the group (file and PROGMAN.INI entry)
 * when it does not exist yet.  *created is set to 1 when the group was
 * created.  Returns 0 = added, 1 = an item with this title is already in the
 * group (nothing changed), -1 = error. */
int gdi_pm_add_item( const char *windir, const char *group, const char *title,
                     const char *command, const char *icon, int index,
                     int *created );

/* Remove the item with this title from the group.  Returns 0 = removed or
 * not present, -1 = error. */
int gdi_pm_remove_item( const char *windir, const char *group,
                        const char *title );

/* Delete a group that has no items left (its file and PROGMAN.INI entry).
 * Returns 0 = deleted or absent, 1 = not empty (kept), -1 = error. */
int gdi_pm_remove_group( const char *windir, const char *group );

/* Convert text from code page 850 (DOS, INSTALL.INI) to ANSI in place. */
void gdi_pm_oem_to_ansi( char *text );
void gdi_pm_ansi_to_oem( char *text );

#endif
