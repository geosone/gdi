/* files.c - GeosOne Dual Installer, copying, INSTALL.LOG, updates, GDI.INI,
 * INSTALL /U
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <io.h>
#include <dos.h>
#include <i86.h>
#include <process.h>
#include <setjmp.h>
#include <malloc.h>
#include "gdi.h"
#include "ui.h"
#include "hexpatch.h"
#include "regtxn.h"
#include "progman.h"
#include "core.h"

/* --- copying ---------------------------------------------------------- */

static const char *progtitle;
static char progname[40];

void gdi_progress( unsigned long done, unsigned long total )
{
	ui_progress( progtitle, progname, done ? done : 1, total ? total : 1 );
}

void gdi_progress_name( const char *name )
{
	const char *b = strrchr( name, '\\' );
	sprintf( progname, "%.39s", b ? b + 1 : name );
}

/* ask for disk n of the archive; 0 = inserted, 1 = cancel */
int gdi_ask_disk( int n )
{
	static const char *b[2];
	char key[4], msg[200];
	int rc;
	b[0] = T( "OK" );
	b[1] = T( "Cancel" );
	sprintf( key, "%d", n );
	sprintf( msg, T( "InsertDisk" ), n, srcdir[0] );
	if ( ini_get( "Disks", key, NULL ) )
		sprintf( msg + strlen( msg ), "\n\n%s", ini_get( "Disks", key, "" ) );
	rc = ui_message( T( "DiskTitle" ), msg, b, 2, 0, 0 );
	ui_clear();
	ui_progress( progtitle, "", 0, 1 );
	return rc != 0;
}

/* --- updates ------------------------------------------------------------ */

#define MAXOLD 800
static char __far *old_log[MAXOLD];   /* far heap: a log can be long */
static int nold;


/* %BOOT%\GDI.INI [Installed] lists every installation that can be removed
 * again (Uninstall=yes): Id=directory|options|version.  Setup finds an
 * earlier installation of the package there and updates it: only the files
 * are replaced.  CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI, WIN.INI, the groups,
 * the registry and the package's settings files ([Keep]) stay as they are. */

int record_read_key( const char *key, char *dir, char *bits, char *version )
{
	return gdi_record_read( boot, key, dir, bits, version );
}

static int record_read( char *dir, char *bits, char *version )
{
	char key[64];
	ini_package_id( key, sizeof(key) );
	return record_read_key( key, dir, bits, version );
}

/* enter (add) or remove this installation in GDI.INI */
void record_write( int add )
{
	char key[64], value[120], bits[MAXOPT + 1];
	int i;
	ini_package_id( key, sizeof(key) );
	for ( i = 0; i < nopt; i++ )
		bits[i] = opt_on[i] ? '1' : '0';
	bits[nopt] = 0;
	sprintf( value, "%s|%s|%.23s", jdir, bits, ini_get( "Setup", "Version", "" ) );
	gdi_record_write( boot, key, add ? value : NULL );
}

/* an earlier installation of this package: ask, then update it */
void find_update( void )
{
	static const char *b[2];
	static char text[500];
	char dir[80], bits[MAXOPT + 1], p[80];
	const char *version = ini_get( "Setup", "Version", "" );
	int i;
	update_mode = 0;
	if ( dosmode || stricmp( ini_get( "Setup", "Uninstall", "yes" ), "yes" ) ||
		!record_read( dir, bits, old_version ) )
		return;
	path_cat( p, dir, "INSTALL.INI" );
	if ( !file_exists( p ) )
		return;
	sprintf( text, T( "UpdateText" ),
		ini_get( "Setup", "Product", "The software" ), dir );
	if ( *old_version || *version )
		sprintf( text + strlen( text ), "\n\n%s %s\n%s %s",
			T( "UpdateOld" ), *old_version ? old_version : "?",
			T( "UpdateNew" ), *version ? version : "?" );
	b[0] = T( "Update" );
	b[1] = T( "Cancel" );
	ui_clear();
	if ( !in_batch && ui_message( T( "UpdateTitle" ), text, b, 2, 0, 0 ) != 0 )
		abort_setup();
	update_mode = 1;
	dirs_from_setup_dir( dir );
	find_dest();
	old_entries_load( p );   /* before the new INSTALL.INI replaces it */
	for ( i = 0; i < nopt && bits[i]; i++ )
		opt_on[i] = bits[i] == '1';
}

static int wild_match( const char *p, const char *s )
{
	for ( ; *p; p++, s++ ) {
		if ( *p == '*' ) {
			for ( ;; s++ ) {
				if ( wild_match( p + 1, s ) )
					return 1;
				if ( !*s )
					return 0;
			}
		}
		if ( !*s || ( *p != '?' && toupper( (unsigned char)*p ) != toupper( (unsigned char)*s ) ) )
			return 0;
	}
	return !*s;
}

static int old_log_find( const char *path );

/* update: an existing settings file stays ([Keep] cond|pattern; a pattern
 * without a path is a file name, else a path below %DIR% or a full path;
 * without [Keep]: *.INI and *.CFG).  Its log line is kept. */
/* a line cond|pattern of section matches path (a pattern without a path is
 * a file name, else a path below %DIR% or a full path); *any: the section
 * has lines */
static int section_match( const char *section, const char *path, int *any )
{
	const char *v, *t, *name = strrchr( path, '\\' );
	char pat[80];
	int pos = 0, n = strlen( dest ), match = 0;
	name = name ? name + 1 : path;
	*any = 0;
	while ( !match && ( pos = ini_lines( section, pos, &v ) ) >= 0 ) {
		*any = 1;
		if ( ( t = cond( v ) ) == NULL )
			continue;
		expand( pat, t, sizeof(pat) );
		if ( pat[1] == ':' )
			match = wild_match( pat, path );
		else if ( strchr( pat, '\\' ) )
			match = !strnicmp( path, dest, n ) && path[n] == '\\' && wild_match( pat, path + n + 1 );
		else
			match = wild_match( pat, name );
	}
	return match;
}

static int keep_file( const char *path )
{
	const char *name = strrchr( path, '\\' );
	int any, keep, i;
	name = name ? name + 1 : path;
	if ( !update_mode || !file_exists( path ) )
		return 0;
	keep = section_match( "Keep", path, &any );
	if ( !any )
		keep = wild_match( "*.INI", name ) || wild_match( "*.CFG", name );
	if ( keep && logf && ( i = old_log_find( path ) ) >= 0 ) {
		fprintf( logf, "%Fs\n", old_log[i] );
		_ffree( old_log[i] );
		old_log[i] = NULL;
	}
	return keep;
}

/* INSTALL.LOG: one line per installed file, "path|backup" when an existing
 * file was replaced (INSTALL /U puts the backup back).  The lines of an
 * earlier installation into the same directory are kept: a file installed
 * again keeps its first backup, so the original is restored. */
static void old_log_load( const char *path )
{
	FILE *f = fopen( path, "r" );
	char l[180];
	nold = 0;
	if ( !f )
		return;
	while ( nold < MAXOLD && fgets( l, sizeof(l), f ) ) {
		char __far *p;
		strip_eol( l );
		if ( !*l || !( p = _fmalloc( strlen( l ) + 1 ) ) )
			continue;
		_fstrcpy( p, l );
		old_log[nold++] = p;
	}
	fclose( f );
}

/* the old line for path (up to '|'), or -1 */
static int old_log_find( const char *path )
{
	int i, n = strlen( path );
	for ( i = 0; i < nold; i++ ) {
		const char __far *l = old_log[i];
		int k;
		if ( !l )
			continue;
		for ( k = 0; k < n && l[k] && toupper( (unsigned char)l[k] ) == toupper( (unsigned char)path[k] ); k++ );
		if ( k == n && ( !l[k] || l[k] == '|' ) )
			return i;
	}
	return -1;
}

/* the lines of the old log that were not installed again */
static void old_log_flush( void )
{
	int i;
	for ( i = 0; i < nold; i++ )
		if ( old_log[i] ) {
			if ( logf )
				fprintf( logf, "%Fs\n", old_log[i] );
			_ffree( old_log[i] );
			old_log[i] = NULL;
		}
	nold = 0;
}

/* before a file is written: log it, keep the file it replaces */
static void log_file( const char *path )
{
	char saved[80];
	int i;
	if ( !logf )
		return;
	if ( ( i = old_log_find( path ) ) >= 0 ) {
		fprintf( logf, "%Fs\n", old_log[i] );
		_ffree( old_log[i] );
		old_log[i] = NULL;
		return;
	}
	backup_as( path, saved );
	if ( *saved )
		fprintf( logf, "%s|%s\n", path, saved );
	else
		fprintf( logf, "%s\n", path );
}

/* --- [Newer]: a file that is there already is replaced only by a newer
 * version (VS_FIXEDFILEINFO of its version resource) -------------------- */

/* the file version of a Windows program or DLL: 0 = found */
int file_version( const char *path, unsigned long *ms, unsigned long *ls )
{
	static unsigned char buf[520];
	FILE *f = fopen( path, "rb" );
	int n, keep = 0, i;
	if ( !f )
		return -1;
	/* the signature BD 04 EF FE, then the structure version, MS and LS */
	while ( ( n = fread( buf + keep, 1, sizeof(buf) - keep, f ) ) > 0 ) {
		n += keep;
		for ( i = 0; i + 16 <= n; i++ )
			if ( buf[i] == 0xBD && buf[i + 1] == 0x04 && buf[i + 2] == 0xEF && buf[i + 3] == 0xFE ) {
				*ms = *(unsigned long *)( buf + i + 8 );
				*ls = *(unsigned long *)( buf + i + 12 );
				fclose( f );
				return 0;
			}
		keep = n < 15 ? n : 15;
		memmove( buf, buf + n - keep, keep );
	}
	fclose( f );
	return -1;
}

/* [Newer] names path, the file is there and new is not newer: keep it */
static int older_file( const char *new_file, const char *path )
{
	unsigned long nms, nls, oms, ols;
	int any;
	if ( !file_exists( path ) || !section_match( "Newer", path, &any ) ||
		file_version( new_file, &nms, &nls ) || file_version( path, &oms, &ols ) )
		return 0;
	return nms < oms || ( nms == oms && nls <= ols );
}

/* archive files of [Newer] go to a temporary file first */
#define MAXNEWER 40
static char *newer_tmp[MAXNEWER], *newer_path[MAXNEWER];
static int nnewer;

/* [Files] line "cond|PATTERN=DEST": PATTERN is a name or "DIR\*" (all
 * archive files below DIR\, "*" = all); DEST is a name or directory,
 * relative to %DIR% unless it is a full path.  Returns 1 if name matches
 * and sets the target path. */
static int file_target( const char *line, const char *name, char *path )
{
	char pat[80], dst[80];
	const char *eq = strchr( line, '=' ), *rest;
	int l, wild;
	sprintf( pat, "%.*s", eq ? (int)( eq - line ) : 79, line );
	l = strlen( pat );
	wild = l && pat[l - 1] == '*';
	if ( wild ) {
		if ( strnicmp( name, pat, l - 1 ) )
			return 0;
		rest = name + l - 1;
	} else {
		if ( stricmp( name, pat ) )
			return 0;
		rest = name;
	}
	if ( eq ) {
		expand( dst, eq + 1, sizeof(dst) );
		if ( dst[1] != ':' ) {
			char t[80];
			path_cat( t, dest, dst );
			strcpy( dst, t );
		}
		if ( wild || ( *dst && dst[strlen( dst ) - 1] == '\\' ) )   /* NAME=DIR\ */
			path_cat( path, dst, rest );
		else
			strcpy( path, dst );
	} else
		path_cat( path, dest, wild ? rest : name );
	return 1;
}

static void make_parent( const char *path )
{
	char d[80], *b;
	strcpy( d, path );
	if ( ( b = strrchr( d, '\\' ) ) != NULL && b - d > 2 ) {
		*b = 0;
		mkdirs( d );
	}
}

/* target of an archive file (callback of arc_install): 0 = install */
static int arc_target( const char *name, char *path )
{
	const char *v, *t;
	int pos = 0;
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL && file_target( t, name, path ) ) {
			make_parent( path );
			if ( keep_file( path ) )
				return -1;
			if ( file_exists( path ) && nnewer < MAXNEWER ) {
				int any;
				if ( section_match( "Newer", path, &any ) ) {
					char tmp[80];
					sprintf( tmp, "%s\\GDINEW.%03d", dest, nnewer );
					newer_path[nnewer] = str_dup( path );
					newer_tmp[nnewer++] = str_dup( tmp );
					strcpy( path, tmp );
					return 0;
				}
			}
			log_file( path );
			return 0;
		}
	return -1;
}

void copy_all( void )
{
	const char *v, *t, *arc = ini_get( "Setup", "Archive", NULL );
	char from[80], to[80], name[40];
	int pos = 0, total = 0, done = 0, i;
	int unin = !stricmp( ini_get( "Setup", "Uninstall", dosmode ? "no" : "yes" ), "yes" );

	progtitle = T( "CopyTitle" );
	if ( unin ) {
		path_cat( to, jdir, "INSTALL.LOG" );
		old_log_load( to );
		logf = fopen( to, "w" );
		if ( *dest_top ) {
			FILE *f;
			path_cat( to, jdir, "DIRS.LOG" );
			if ( ( f = fopen( to, "w" ) ) != NULL ) {
				fprintf( f, "%s\n", dest_top );
				fclose( f );
			}
		}
	}
	/* [Dirs]: directories to create (cond|path) */
	while ( ( pos = ini_lines( "Dirs", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( to, t, sizeof(to) );
			mkdirs( to );
		}
	pos = 0;
	/* plain files next to INSTALL.EXE */
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 )
		if ( cond( v ) )
			total++;
	if ( unin )
		total += 2;
	pos = 0;
	ui_progress( progtitle, "", 0, total );
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
		if ( !( t = cond( v ) ) )
			continue;
		sprintf( name, "%.*s", strchr( t, '=' ) ? (int)( strchr( t, '=' ) - t ) : 39, t );
		if ( strchr( name, '*' ) )
			continue;
		path_cat( from, srcdir, name );
		if ( arc && !file_exists( from ) )
			continue;   /* in the archive */
		file_target( t, name, to );
		make_parent( to );
		ui_progress( progtitle, name, ++done, total );
		if ( keep_file( to ) || older_file( from, to ) )
			continue;
		log_file( to );
		if ( copy_file( from, to ) ) {
			char msg[160];
			sprintf( msg, "%s %s -> %s", T( "CopyError" ), from, to );
			fail( msg );
		}
	}
	/* [Find] files from the installation disk that are used */
	for ( i = 0; i < nfind; i++ )
		if ( finds[i].file[0] && find_used( finds[i].name ) ) {
			path_cat( from, srcdir, finds[i].file );
			ui_progress( progtitle, finds[i].file, done, total );
			log_file( finds[i].path );
			if ( copy_file( from, finds[i].path ) ) {
				char msg[160];
				sprintf( msg, "%s %s -> %s", T( "CopyError" ), from, finds[i].path );
				fail( msg );
			}
		}
	if ( unin ) {
		/* INSTALL /U in the target directory removes the installation */
		path_cat( from, srcdir, "INSTALL.EXE" );
		path_cat( to, jdir, "INSTALL.EXE" );
		ui_progress( progtitle, "INSTALL.EXE", ++done, total );
		copy_file( from, to );
		path_cat( from, srcdir, "INSTALL.INI" );
		path_cat( to, jdir, "INSTALL.INI" );
		ui_progress( progtitle, "INSTALL.INI", ++done, total );
		copy_file( from, to );
	}
	if ( arc ) {
		char base[80];
		const char *err;
		path_cat( base, srcdir, arc );
		ui_clear();
		ui_progress( progtitle, "", 0, 1 );
		if ( ( err = arc_install( base, arc_target ) ) != NULL ) {
			char msg[160];
			if ( !strcmp( err, "Cancelled" ) )
				abort_setup();
			if ( !strcmp( err, "CreateError" ) )
				sprintf( msg, "%s %s", T( "CopyError" ), arc_errfile() );
			else if ( !strcmp( err, "CrcError" ) || !strcmp( err, "BadArchive" ) || !strcmp( err, "ReadError" ) )
				sprintf( msg, "%s (%s)", T( "DiskError" ), progname );
			else
				sprintf( msg, "%s", T( err ) );
			fail( msg );
		}
		/* [Newer]: the newer one of each pair stays */
		for ( i = 0; i < nnewer; i++ ) {
			if ( !older_file( newer_tmp[i], newer_path[i] ) ) {
				log_file( newer_path[i] );
				remove( newer_path[i] );
				if ( rename( newer_tmp[i], newer_path[i] ) && copy_file( newer_tmp[i], newer_path[i] ) ) {
					char msg[160];
					sprintf( msg, "%s %s", T( "CopyError" ), newer_path[i] );
					fail( msg );
				}
			}
			remove( newer_tmp[i] );
			free( newer_tmp[i] );
			free( newer_path[i] );
		}
		nnewer = 0;
	}
	old_log_flush();
	if ( logf ) {
		fclose( logf );
		logf = NULL;
	}
}

/* [Run] Before= / After=: programs to run (cond|command line); a program
 * without a path is taken from the directory of INSTALL.EXE */
int run_program( const char *cmdline )
{
	int rc;
	ui_exit();
	rc = spawn_program( cmdline );
	ui_init();
	ui_title( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne Dual Installer" ) );
	ui_clear();
	return rc;
}

void run_all( const char *key )
{
	const char *v, *t;
	int pos = 0;
	while ( ( pos = ini_next( "Run", key, pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL && run_program( t ) == -1 ) {
			char msg[160];
			sprintf( msg, "%s %s", T( "RunError" ), t );
			fail( msg );
		}
}

void edit_all( void )
{
	char p[80];
	sprintf( p, "%s\\CONFIG.SYS", boot );
	edit_file( p, "Config.sys", 0 );
	sprintf( p, "%s\\AUTOEXEC.BAT", boot );
	edit_file( p, "Autoexec.bat", 1 );
	if ( windir[0] ) {
		path_cat( p, windir, "SYSTEM.INI" );
		if ( file_exists( p ) )
			edit_file( p, "System.ini", 2 );
		path_cat( p, windir, "WIN.INI" );
		if ( !frontend() && file_exists( p ) )
			edit_file( p, "Win.ini", 3 );
	}
	ini_files();
}

/* --- a file replaced by a later package ----------------------------------
 * Two packages that replace the same file: the later one keeps the earlier
 * one's file as its backup.  Removing the earlier one first must not touch
 * the file; it hands its own backup (the original) to the later one, or,
 * when it created the file, tells it there is nothing to put back. */

static int backup_number( const char *backup )
{
	int n = strlen( backup );
	return n > 4 && backup[n - 4] == '.' && toupper( (unsigned char)backup[n - 3] ) == 'G' ?
		atoi( backup + n - 2 ) : 0;
}

/* the package installed after this one that replaced path next: its
 * INSTALL.LOG and backup; 0: none */
static int later_owner( const char *path, const char *mine, char *log, char *theirs )
{
	FILE *g, *f;
	char p[80], l[180], d[80], *e;
	int in = 0, n = strlen( path ), my = *mine ? backup_number( mine ) : 0, best = 0, k;
	sprintf( p, "%s\\GDI.INI", boot );
	if ( !( g = fopen( p, "r" ) ) )
		return 0;
	while ( fgets( l, sizeof(l), g ) ) {
		strip_eol( l );
		if ( *l == '[' ) {
			in = !strnicmp( l, "[Installed]", 11 );
			continue;
		}
		if ( !in || !( e = strchr( l, '=' ) ) )
			continue;
		sprintf( d, "%.79s", e + 1 );
		if ( ( e = strchr( d, '|' ) ) != NULL )
			*e = 0;
		if ( !stricmp( d, dest ) )
			continue;
		path_cat( p, d, "INSTALL.LOG" );
		if ( !( f = fopen( p, "r" ) ) )
			continue;
		while ( fgets( l, sizeof(l), f ) ) {
			strip_eol( l );
			if ( strnicmp( l, path, n ) || l[n] != '|' )
				continue;
			k = backup_number( l + n + 1 );
			if ( k > my && ( !best || k < best ) ) {
				best = k;
				strcpy( log, p );
				sprintf( theirs, "%.79s", l + n + 1 );
			}
			break;
		}
		fclose( f );
	}
	fclose( g );
	return best != 0;
}

/* the line "path|backup" of another INSTALL.LOG becomes "path" (in
 * old_log, unused while a package is removed) */
static void log_drop_backup( const char *log, const char *path )
{
	char __far **lines = old_log;
	char l[180];
	FILE *f = fopen( log, "r" );
	int n = 0, i, k = strlen( path );
	if ( !f )
		return;
	while ( n < MAXOLD && fgets( l, sizeof(l), f ) ) {
		strip_eol( l );
		if ( !strnicmp( l, path, k ) && l[k] == '|' )
			l[k] = 0;
		if ( ( lines[n] = _fmalloc( strlen( l ) + 1 ) ) != NULL )
			_fstrcpy( lines[n++], l );
	}
	fclose( f );
	if ( ( f = fopen( log, "w" ) ) != NULL ) {
		for ( i = 0; i < n; i++ )
			fprintf( f, "%Fs\n", lines[i] );
		fclose( f );
	}
	while ( n )
		_ffree( lines[--n] );
}

/* path (with this package's backup, or "") was replaced again by a later
 * package: hand it over; 1: the file stays */
static int hand_over_file( const char *path, const char *mine )
{
	char log[80], theirs[80];
	if ( !later_owner( path, mine, log, theirs ) )
		return 0;
	if ( *mine && file_exists( mine ) ) {
		/* its backup (our file) is replaced by ours (the original) */
		remove( theirs );
		if ( rename( mine, theirs ) && !copy_file( mine, theirs ) )
			remove( mine );
	} else {
		/* we created the file: the later package removes it */
		remove( theirs );
		log_drop_backup( log, path );
	}
	return 1;
}

void remove_all( void )
{
	const char *v, *t;
	char p[80], name[40];
	int pos = 0, had_log = 0, pending;
	static const char *b[2];
	b[0] = T( "Remove" );
	b[1] = T( "Cancel" );
	if ( !in_batch && ui_message( T( "UninstTitle" ), T( "UninstText" ), b, 2, 1, 0 ) != 0 ) {
		ui_clear();
		abort_setup();
	}
	dirs_from_setup_dir( srcdir );
	for ( pos = 0; pos < MAXOPT; pos++ )
		opt_on[pos] = 1;
	find_windows();
	run_all( "Uninstall" );   /* the package's own undo, while its files are there */
	patch_all();
	edit_all();
	progman_undo();
	pending = uninstall_registry();
	path_cat( p, jdir, "INSTALL.LOG" );
	if ( ( logf = fopen( p, "r" ) ) != NULL ) {
		/* the files installed (from archives: also below %DIR%); a
		 * replaced file comes back from its backup */
		char l[180];
		had_log = 1;
		while ( fgets( l, sizeof(l), logf ) ) {
			char *e, *b;
			strip_eol( l );
			if ( ( e = strchr( l, '|' ) ) != NULL ) {
				*e++ = 0;
				if ( hand_over_file( l, e ) )
					continue;
				if ( file_exists( e ) ) {
					remove( l );
					if ( rename( e, l ) && !copy_file( e, l ) )
						remove( e );
				}
				continue;
			}
			runner_path( p );
			if ( pending && !stricmp( l, p ) )
				continue;   /* runs at the next start of Windows */
			if ( hand_over_file( l, "" ) )
				continue;
			remove( l );
			while ( ( b = strrchr( l, '\\' ) ) != NULL && b - l > 2 && stricmp( l, dest ) ) {
				*b = 0;
				if ( rmdir( l ) )
					break;
			}
		}
		fclose( logf );
		logf = NULL;
		path_cat( p, jdir, "INSTALL.LOG" );
		remove( p );
	}
	pos = 0;
	while ( !had_log && ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
		const char *eq;
		t = strchr( v, '|' ) ? strchr( v, '|' ) + 1 : v;
		if ( strchr( t, '*' ) )
			continue;
		eq = strchr( t, '=' );
		strcpy( name, eq ? eq + 1 : t );
		if ( name[1] == ':' )
			strcpy( p, name );
		else
			path_cat( p, dest, name );
		remove( p );
	}
	record_write( 0 );
	if ( !keep_uninstaller && !pending ) {
		path_cat( p, jdir, "INSTALL.INI" );
		remove( p );
		path_cat( p, jdir, "INSTALL.EXE" );
		remove( p );
		gdi_remove_dirs( jdir );
	}
}
