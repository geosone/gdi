#include <stdio.h>
#include <string.h>
#include "regtxn.h"

static int failures;

#define CHECK(x) do { if ( !(x) ) { \
    fprintf( stderr, "FAIL line %d: %s\n", __LINE__, #x ); failures++; \
} } while ( 0 )

static void parser_tests( void )
{
    struct gdi_reg_entry entry;
    unsigned char data[GDI_REG_DATA_MAX];
    unsigned size;
    char error[100];

    CHECK( !gdi_reg_parse( "Set",
        "1,!2,?TAPE|HKLM|Software\\GeosOne\\OpenASPI|Enabled|DWORD|0x12345678",
        &entry, error, sizeof(error) ) );
    CHECK( entry.operation == GDI_REG_SET );
    CHECK( entry.root == GDI_REG_HKLM );
    CHECK( entry.type == GDI_REG_DWORD );
    CHECK( !strcmp( entry.condition, "1,!2,?TAPE" ) );
    CHECK( !strcmp( entry.name, "Enabled" ) );
    CHECK( !gdi_reg_make_data( &entry, data, &size, error, sizeof(error) ) );
    CHECK( size == 4 && data[0] == 0x78 && data[1] == 0x56 &&
           data[2] == 0x34 && data[3] == 0x12 );

    CHECK( !gdi_reg_parse( "Set", "|HKCR|Tape\\shell\\open|@|SZ|Open tape",
                           &entry, error, sizeof(error) ) );
    CHECK( !entry.name[0] );
    CHECK( !gdi_reg_validate_family( &entry, 0, error, sizeof(error) ) );
    CHECK( !gdi_reg_make_data( &entry, data, &size, error, sizeof(error) ) );
    CHECK( size == 10 && !memcmp( data, "Open tape", 10 ) );

    CHECK( !gdi_reg_parse( "Set", "|HKLM|Software\\GDI|Blob|BINARY|00 ff,7A-10",
                           &entry, error, sizeof(error) ) );
    CHECK( !gdi_reg_make_data( &entry, data, &size, error, sizeof(error) ) );
    CHECK( size == 4 && data[0] == 0 && data[1] == 0xff &&
           data[2] == 0x7a && data[3] == 0x10 );
    CHECK( gdi_reg_validate_family( &entry, 0, error, sizeof(error) ) != 0 );
    CHECK( !gdi_reg_validate_family( &entry, 1, error, sizeof(error) ) );

    CHECK( !gdi_reg_parse( "DeleteValue", "!?OLD|HKCU|Software\\GDI|OldValue",
                           &entry, error, sizeof(error) ) );
    CHECK( entry.operation == GDI_REG_DELETE_VALUE );
    CHECK( !gdi_reg_parse( "DeleteKey", "|HKU|.Default\\Software\\GDI",
                           &entry, error, sizeof(error) ) );
    CHECK( entry.operation == GDI_REG_DELETE_KEY );

    CHECK( gdi_reg_parse( "Set", "|BAD|Key|Name|SZ|x", &entry,
                          error, sizeof(error) ) != 0 );
    CHECK( gdi_reg_parse( "Set", "17|HKLM|Key|Name|SZ|x", &entry,
                          error, sizeof(error) ) != 0 );
    CHECK( gdi_reg_parse( "Set", "|HKLM|Key|Name|QWORD|x", &entry,
                          error, sizeof(error) ) != 0 );
    CHECK( gdi_reg_parse( "DeleteKey", "|HKLM|Key|extra", &entry,
                          error, sizeof(error) ) != 0 );
}

static void journal_tests( void )
{
    struct gdi_reg_record before, after;
    char line[GDI_REG_JOURNAL_MAX], error[100];
    char saved[GDI_REG_JOURNAL_MAX], small[4];
    static const unsigned char bytes[] = { 0, 1, '|', '\r', '\n', 0xff };

    memset( &before, 0, sizeof(before) );
    before.kind = 'V';
    before.root = GDI_REG_HKLM;
    before.existed = 1;
    before.type = GDI_REG_BINARY;
    strcpy( before.key, "Software\\GeosOne\\OpenASPI" );
    strcpy( before.name, "Binary|Name" );
    memcpy( before.data, bytes, sizeof(bytes) );
    before.data_size = sizeof(bytes);
    CHECK( !gdi_reg_journal_format( &before, line, sizeof(line) ) );
    strcpy( saved, line );
    strcat( line, "\r\n" );
    CHECK( !gdi_reg_journal_parse( line, &after, error, sizeof(error) ) );
    CHECK( after.kind == before.kind && after.root == before.root &&
           after.existed == before.existed && after.type == before.type );
    CHECK( !strcmp( after.key, before.key ) && !strcmp( after.name, before.name ) );
    CHECK( after.data_size == sizeof(bytes) &&
           !memcmp( after.data, bytes, sizeof(bytes) ) );
    CHECK( gdi_reg_journal_format( &before, small, sizeof(small) ) != 0 );
    CHECK( !strcmp( saved,
        "V1|V|HKLM|536F6674776172655C47656F734F6E655C4F70656E41535049|"
        "42696E6172797C4E616D65|1|BINARY|00017C0D0AFF" ) );

    memset( &before, 0, sizeof(before) );
    before.kind = 'K';
    before.root = GDI_REG_HKCR;
    before.existed = 0;
    before.type = GDI_REG_NONE;
    strcpy( before.key, "Tape\\shell" );
    CHECK( !gdi_reg_journal_format( &before, line, sizeof(line) ) );
    CHECK( !gdi_reg_journal_parse( line, &after, error, sizeof(error) ) );
    CHECK( after.kind == 'K' && !after.existed &&
           !strcmp( after.key, "Tape\\shell" ) );

    CHECK( gdi_reg_journal_parse( "V1|V|HKLM|0|00|1|BINARY|GG",
                                  &after, error, sizeof(error) ) != 0 );
}

int main( void )
{
    parser_tests();
    journal_tests();
    if ( failures )
        return 1;
    puts( "registry transaction tests passed" );
    return 0;
}
