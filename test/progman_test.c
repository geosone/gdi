#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "progman.h"

static int failures;
static char dir[200];

#define CHECK(x) do { if ( !(x) ) { \
    fprintf( stderr, "FAIL line %d: %s\n", __LINE__, #x ); failures++; \
} } while ( 0 )

static unsigned char file[70000];
static long size;

static unsigned w( long at )
{
    return file[at] | ( (unsigned)file[at + 1] << 8 );
}

static int load( const char *name )
{
    char path[300];
    FILE *f;
    sprintf( path, "%s/%s", dir, name );
    if ( !( f = fopen( path, "rb" ) ) )
        return -1;
    size = (long)fread( file, 1, sizeof(file), f );
    fclose( f );
    return 0;
}

static void save( const char *name, const char *text )
{
    char path[300];
    FILE *f;
    sprintf( path, "%s/%s", dir, name );
    f = fopen( path, "w" );
    fputs( text, f );
    fclose( f );
}

static int exists( const char *name )
{
    char path[300];
    FILE *f;
    sprintf( path, "%s/%s", dir, name );
    if ( !( f = fopen( path, "rb" ) ) )
        return 0;
    fclose( f );
    return 1;
}

/* a valid group file: checksum, offsets inside, strings terminated */
static int valid( void )
{
    unsigned sum = 0, i, n, cb;
    long at;
    if ( size < 34 || memcmp( file, "PMCC", 4 ) )
        return 0;
    for ( at = 0; at + 1 < size; at += 2 )
        sum += w( at );
    if ( size & 1 )
        sum += file[size - 1];
    if ( sum & 0xFFFF )
        return 0;
    cb = w( 6 );
    n = w( 32 );
    if ( cb > size || 34 + 2 * n > cb || w( 22 ) >= cb )
        return 0;
    for ( i = 0; i < n; i++ ) {
        unsigned item = w( 34 + 2 * i ), k;
        if ( !item )
            continue;
        if ( item + 24 > cb )
            return 0;
        for ( k = 18; k <= 22; k += 2 )
            if ( w( item + k ) >= cb || !memchr( file + w( item + k ), 0, cb - w( item + k ) ) )
                return 0;
    }
    return 1;
}

/* number of items, and whether one has this title and command */
static int items( const char *title, const char *command )
{
    unsigned i, n = w( 32 ), count = 0, found = 0;
    for ( i = 0; i < n; i++ ) {
        unsigned item = w( 34 + 2 * i );
        if ( !item )
            continue;
        count++;
        if ( title && !strcmp( (char *)file + w( item + 18 ), title ) &&
             !strcmp( (char *)file + w( item + 20 ), command ) )
            found = 1;
    }
    return title ? (int)found : (int)count;
}

/* an old group with one item that has icon data, as Program Manager writes it */
static void old_group( void )
{
    unsigned char g[400];
    unsigned at = 34 + 2, sum = 0, i;
    char path[300];
    FILE *f;
    memset( g, 0, sizeof(g) );
    memcpy( g, "PMCC", 4 );
    g[8] = 1;
    g[22] = (unsigned char)at;
    g[24] = 32; g[26] = 32; g[28] = 1; g[29] = 4;
    g[32] = 1;
    strcpy( (char *)g + at, "Haupt" );
    at += 6;
    g[34] = (unsigned char)at;           /* item */
    g[at] = 34;
    g[at + 6] = 12; g[at + 8] = 128; g[at + 10] = 0; g[at + 11] = 2;
    g[at + 12] = (unsigned char)( at + 24 + 18 );  /* pHeader */
    g[at + 14] = (unsigned char)( at + 24 + 30 );  /* pAND (fake) */
    g[at + 16] = (unsigned char)( at + 24 + 31 );  /* pXOR (fake) */
    g[at + 18] = (unsigned char)( at + 24 );
    g[at + 20] = (unsigned char)( at + 24 + 5 );
    g[at + 22] = (unsigned char)( at + 24 + 5 + 7 );
    strcpy( (char *)g + at + 24, "Alt1" );
    strcpy( (char *)g + at + 24 + 5, "A1.EXE" );
    strcpy( (char *)g + at + 24 + 12, "A1.EXE" );
    at += 24 + 19 + 13;
    g[6] = (unsigned char)at;
    for ( i = 0; i < at; i += 2 )
        sum += g[i] | ( g[i + 1] << 8 );
    sum = ( 0x10000 - ( sum & 0xFFFF ) ) & 0xFFFF;
    g[4] = (unsigned char)( sum & 0xFF );
    g[5] = (unsigned char)( sum >> 8 );
    sprintf( path, "%s/HAUPT.GRP", dir );
    f = fopen( path, "wb" );
    fwrite( g, 1, at, f );
    fclose( f );
}

int main( int argc, char **argv )
{
    int created;
    char text[100];
    if ( argc < 2 )
        return 2;
    sprintf( dir, "%s", argv[1] );

    old_group();
    save( "PROGMAN.INI", "[Settings]\nOrder= 1\n\n[Groups]\nGroup1=HAUPT.GRP\n" );
    CHECK( load( "HAUPT.GRP" ) == 0 && valid() && items( NULL, NULL ) == 1 );

    /* a new item in an existing group: its table grows by one slot */
    CHECK( gdi_pm_add_item( dir, "haupt", "Neu", "C:\\WINDOWS\\NEU.EXE", "C:\\WINDOWS\\NEU.EXE", 0, &created ) == 0 );
    CHECK( !created );
    CHECK( load( "HAUPT.GRP" ) == 0 && valid() );
    CHECK( items( NULL, NULL ) == 2 && items( "Neu", "C:\\WINDOWS\\NEU.EXE" ) && items( "Alt1", "A1.EXE" ) );
    CHECK( w( 24 ) == 0 && w( 28 ) == 0 );   /* icons are extracted again */
    CHECK( !strcmp( (char *)file + w( 22 ), "Haupt" ) );
    /* the old item's icon offsets moved with it */
    CHECK( w( w( 34 ) + 12 ) == w( w( 34 ) + 18 ) + 18 );
    /* the same title again: nothing changes */
    CHECK( gdi_pm_add_item( dir, "Haupt", "Neu", "X.EXE", "X.EXE", 0, &created ) == 1 );

    /* a new group: file, Group2= and Order= */
    CHECK( gdi_pm_add_item( dir, "Zubeh\xf6r Neu", "Rechner", "CALC.EXE", "CALC.EXE", 1, &created ) == 0 );
    CHECK( created );
    CHECK( exists( "ZUBEHRNE.GRP" ) );
    CHECK( load( "ZUBEHRNE.GRP" ) == 0 && valid() && items( "Rechner", "CALC.EXE" ) );
    CHECK( gdi_pm_add_item( dir, "Zubeh\xf6r Neu", "Uhr", "CLOCK.EXE", "CLOCK.EXE", 0, &created ) == 0 && !created );
    CHECK( load( "ZUBEHRNE.GRP" ) == 0 && valid() && items( NULL, NULL ) == 2 );
    /* the two items do not share a place */
    CHECK( w( w( 34 ) ) != w( w( 36 ) ) || w( w( 34 ) + 2 ) != w( w( 36 ) + 2 ) );
    CHECK( load( "PROGMAN.INI" ) == 0 && strstr( (char *)file, "Group2=" ) &&
           strstr( (char *)file, "ZUBEHRNE.GRP" ) && strstr( (char *)file, "Order= 1 2" ) );

    /* removing: the group stays while it has items */
    CHECK( gdi_pm_remove_item( dir, "Zubeh\xf6r Neu", "Rechner" ) == 0 );
    CHECK( load( "ZUBEHRNE.GRP" ) == 0 && valid() && items( NULL, NULL ) == 1 );
    CHECK( gdi_pm_remove_group( dir, "Zubeh\xf6r Neu" ) == 1 && exists( "ZUBEHRNE.GRP" ) );
    CHECK( gdi_pm_remove_item( dir, "Zubeh\xf6r Neu", "Uhr" ) == 0 );
    CHECK( gdi_pm_remove_group( dir, "Zubeh\xf6r Neu" ) == 0 && !exists( "ZUBEHRNE.GRP" ) );
    CHECK( load( "PROGMAN.INI" ) == 0 && !strstr( (char *)file, "Group2=" ) &&
           strstr( (char *)file, "Order= 1" ) && !strstr( (char *)file, " 2" ) );
    /* the slot freed in HAUPT is used again */
    CHECK( gdi_pm_remove_item( dir, "Haupt", "Neu" ) == 0 );
    CHECK( load( "HAUPT.GRP" ) == 0 && valid() && items( NULL, NULL ) == 1 );
    CHECK( gdi_pm_add_item( dir, "Haupt", "Wieder", "W.EXE", "W.EXE", 0, &created ) == 0 );
    CHECK( load( "HAUPT.GRP" ) == 0 && valid() && w( 32 ) == 2 && items( NULL, NULL ) == 2 );
    /* absent group or item: nothing to do */
    CHECK( gdi_pm_remove_item( dir, "Fehlt", "X" ) == 0 );
    CHECK( gdi_pm_remove_group( dir, "Fehlt" ) == 0 );

    strcpy( text, "Zubeh\x94r \x84\x81\xe1" );
    gdi_pm_oem_to_ansi( text );
    CHECK( !strcmp( text, "Zubeh\xf6r \xe4\xfc\xdf" ) );

    if ( failures )
        return 1;
    printf( "progman tests passed\n" );
    return 0;
}
