/* winreg16.h - Windows 3.x registry transaction backend for GDI */

#ifndef GDI_WINREG16_H
#define GDI_WINREG16_H

int gdi_reg16_apply_plan( const char *plan_path, char *error,
                          unsigned error_size );
int gdi_reg16_restore_journal( const char *journal_path, char *error,
                               unsigned error_size );

#endif
