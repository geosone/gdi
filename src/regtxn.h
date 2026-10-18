/* regtxn.h - portable registry plan and journal format for GDI
 * Copyright (C) 2026 GeosOne. GNU General Public License version 3. */

#ifndef GDI_REGTXN_H
#define GDI_REGTXN_H

#include <stddef.h>

#define GDI_REG_CONDITION_MAX  63
#define GDI_REG_KEY_MAX       255
#define GDI_REG_NAME_MAX      127
#define GDI_REG_TEXT_MAX      511
#define GDI_REG_DATA_MAX      512
#define GDI_REG_JOURNAL_MAX  1900

enum gdi_reg_operation {
    GDI_REG_SET = 1,
    GDI_REG_DELETE_VALUE,
    GDI_REG_DELETE_KEY
};

enum gdi_reg_root {
    GDI_REG_HKCR = 1,
    GDI_REG_HKCU,
    GDI_REG_HKLM,
    GDI_REG_HKU
};

enum gdi_reg_type {
    GDI_REG_NONE = 0,
    GDI_REG_SZ = 1,
    GDI_REG_EXPAND_SZ = 2,
    GDI_REG_BINARY = 3,
    GDI_REG_DWORD = 4
};

struct gdi_reg_entry {
    int operation;
    int root;
    int type;
    char condition[GDI_REG_CONDITION_MAX + 1];
    char key[GDI_REG_KEY_MAX + 1];
    char name[GDI_REG_NAME_MAX + 1];
    char text[GDI_REG_TEXT_MAX + 1];
};

/* A value record preserves the exact previous type/data.  A key record says
 * whether an empty key existed, so rollback can recreate a deleted key or
 * remove a key created by setup only when it is still empty. */
struct gdi_reg_record {
    char kind;                 /* 'V' = value, 'K' = empty key */
    int root;
    int existed;
    int type;
    unsigned data_size;
    char key[GDI_REG_KEY_MAX + 1];
    char name[GDI_REG_NAME_MAX + 1];
    unsigned char data[GDI_REG_DATA_MAX];
};

int gdi_reg_parse( const char *verb, const char *value,
                   struct gdi_reg_entry *entry, char *error,
                   size_t error_size );
int gdi_reg_validate_family( const struct gdi_reg_entry *entry, int win9x,
                             char *error, size_t error_size );
int gdi_reg_make_data( const struct gdi_reg_entry *entry, unsigned char *data,
                       unsigned *data_size, char *error, size_t error_size );

int gdi_reg_journal_format( const struct gdi_reg_record *record, char *line,
                            size_t line_size );
int gdi_reg_journal_parse( const char *line, struct gdi_reg_record *record,
                           char *error, size_t error_size );

const char *gdi_reg_root_name( int root );
const char *gdi_reg_type_name( int type );

#endif
