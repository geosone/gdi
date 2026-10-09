/* install.c - GeosOne DOS Installer (INSTALL.EXE): installs a DOS program
 * or driver as described by INSTALL.INI (next to INSTALL.EXE):
 * welcome text, license to accept, target directory, options, files to
 * copy, lines to add to / remove from CONFIG.SYS, AUTOEXEC.BAT and the
 * SYSTEM.INI of Windows 3.x.  INSTALL /U removes the installation again
 * (run from the target directory).  See README.md for INSTALL.INI.
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#include <io.h>
#include <dos.h>
#include <i86.h>
#include "gdi.h"

#define MAXOPT   16
#define MAXLINE  600

static char srcdir[80], dest[80], windir[80], boot[3] = "C:";
static char opt_on[MAXOPT];
static const char *opt_label[MAXOPT];
static char opt_value[MAXOPT][24];   /* %On%: value when option n is on */
static int nopt;
static int uninstall;

static const char *T( const char *key, const char *def )
{
	return ini_get( "Text", key, def );
}

/* --- helpers -------------------------------------------------------- */

static void path_cat( char *dst, const char *dir, const char *name )
{
	strcpy( dst, dir );
	if ( *dst && dst[strlen( dst ) - 1] != '\\' )
		strcat( dst, "\\" );
	strcat( dst, name );
}

static int file_exists( const char *p )
{
	return access( p, 0 ) == 0;
}

/* %DIR%, %WINDIR%, %BOOT%, %SRC% */
static void expand( char *out, const char *in, int max )
{
	int o = 0;
	while ( *in && o < max - 1 ) {
		const char *rep = NULL;
		int l = 0;
		if ( !strnicmp( in, "%DIR%", 5 ) ) { rep = dest; l = 5; }
		else if ( !strnicmp( in, "%WINDIR%", 8 ) ) { rep = windir; l = 8; }
		else if ( !strnicmp( in, "%BOOT%", 6 ) ) { rep = boot; l = 6; }
		else if ( !strnicmp( in, "%SRC%", 5 ) ) { rep = srcdir; l = 5; }
		else if ( in[0] == '%' && ( in[1] == 'O' || in[1] == 'o' ) && isdigit( in[2] ) ) {
			int n = atoi( in + 2 ), d = 2;
			while ( isdigit( in[d] ) )
				d++;
			if ( in[d] == '%' && n >= 1 && n <= nopt ) {
				rep = opt_on[n - 1] ? opt_value[n - 1] : "";
				l = d + 1;
			}
		}
		if ( rep ) {
			while ( *rep && o < max - 1 )
				out[o++] = *rep++;
			in += l;
		} else
			out[o++] = *in++;
	}
	out[o] = 0;
}

/* "1,!2|text": options 1 on and 2 off; returns the text or NULL */
static const char *cond( const char *v )
{
	const char *bar = strchr( v, '|' ), *p = v;
	if ( !bar )
		return v;
	while ( p < bar ) {
		int neg = 0, n;
		while ( *p == ' ' || *p == ',' )
			p++;
		if ( p >= bar )
			break;
		if ( *p == '!' ) {
			neg = 1;
			p++;
		}
		n = atoi( p );
		while ( p < bar && isdigit( *p ) )
			p++;
		if ( n >= 1 && n <= nopt && ( opt_on[n - 1] != 0 ) == neg )
			return NULL;
	}
	return bar + 1;
}

static int contains( const char *s, const char *pat )
{
	int l = strlen( pat );
	for ( ; *s; s++ )
		if ( !strnicmp( s, pat, l ) )
			return 1;
	return 0;
}

static int mkdirs( const char *path )
{
	char p[80];
	int i;
	strcpy( p, path );
	for ( i = 3; p[i]; i++ )
		if ( p[i] == '\\' ) {
			p[i] = 0;
			mkdir( p );
			p[i] = '\\';
		}
	mkdir( p );
	return file_exists( p ) ? 0 : -1;
}

static int copy_file( const char *from, const char *to )
{
	FILE *in = fopen( from, "rb" ), *out;
	static char buf[4096];
	size_t n;
	if ( !in )
		return -1;
	if ( !( out = fopen( to, "wb" ) ) ) {
		fclose( in );
		return -1;
	}
	while ( ( n = fread( buf, 1, sizeof(buf), in ) ) > 0 )
		if ( fwrite( buf, 1, n, out ) != n ) {
			fclose( in );
			fclose( out );
			return -1;
		}
	fclose( in );
	return fclose( out ) ? -1 : 0;
}

static void fail( const char *text )
{
	static const char *ok[] = { "OK" };
	scr_message( T( "ErrorTitle", "Error" ), text, ok, 1, 0, 1 );
	scr_exit();
	exit( 2 );
}

static void abort_setup( void )
{
	static const char *ok[] = { "OK" };
	scr_message( T( "AbortTitle", "Setup" ), T( "Aborted", "Setup was cancelled. Nothing was changed." ), ok, 1, 0, 0 );
	scr_exit();
	exit( 1 );
}

/* --- text file editing (CONFIG.SYS, AUTOEXEC.BAT, SYSTEM.INI) -------- */

static char *ed[MAXLINE];
static int ned;

static int ed_load( const char *path )
{
	FILE *f = fopen( path, "r" );
	char buf[256];
	ned = 0;
	if ( !f )
		return -1;
	while ( ned < MAXLINE && fgets( buf, sizeof(buf), f ) ) {
		int l = strlen( buf );
		while ( l && ( buf[l - 1] == '\n' || buf[l - 1] == '\r' || buf[l - 1] == 0x1A ) )
			buf[--l] = 0;
		ed[ned++] = str_dup( buf );
	}
	fclose( f );
	return 0;
}

static void ed_insert( int at, const char *s )
{
	int i;
	if ( ned >= MAXLINE )
		return;
	for ( i = ned; i > at; i-- )
		ed[i] = ed[i - 1];
	ed[at] = str_dup( s );
	ned++;
}

static void ed_delete( int at )
{
	int i;
	free( ed[at] );
	for ( i = at; i < ned - 1; i++ )
		ed[i] = ed[i + 1];
	ned--;
}

/* keep a copy of the original: NAME.G01, NAME.G02, ... */
static void backup( const char *path )
{
	char b[80], *dot;
	int i;
	if ( !file_exists( path ) )
		return;
	strcpy( b, path );
	dot = strrchr( b, '.' );
	if ( !dot || strchr( dot, '\\' ) )
		dot = b + strlen( b );
	for ( i = 1; i < 100; i++ ) {
		sprintf( dot, ".G%02d", i );
		if ( !file_exists( b ) ) {
			copy_file( path, b );
			return;
		}
	}
}

static int ed_save( const char *path )
{
	FILE *f;
	int i;
	backup( path );
	if ( !( f = fopen( path, "w" ) ) )
		return -1;
	for ( i = 0; i < ned; i++ )
		fprintf( f, "%s\n", ed[i] );
	return fclose( f );
}

static void ed_free( void )
{
	while ( ned )
		free( ed[--ned] );
}

/* remove the lines matching any Remove= of section; returns the count */
static int ed_remove( const char *section )
{
	const char *v;
	int pos = 0, i, n = 0;
	while ( ( pos = ini_next( section, "Remove", pos, &v ) ) >= 0 )
		for ( i = 0; i < ned; i++ )
			if ( contains( ed[i], v ) && ed[i][0] != '[' ) {
				ed_delete( i-- );
				n++;
			}
	return n;
}

/* CONFIG.SYS / AUTOEXEC.BAT: lines of Add= (conditional) at the top or
 * bottom; with a [menu] (multi-config CONFIG.SYS) into the [common] block;
 * in AUTOEXEC.BAT before a line that starts Windows */
static void ed_add_dos( const char *section, int isconfig )
{
	const char *v, *t;
	char line[256];
	/* default: CONFIG.SYS at the end, AUTOEXEC.BAT at the top (it may end
	 * with a menu, a shell or Windows) */
	int pos = 0, at, top = !stricmp( ini_get( section, "Position", isconfig ? "bottom" : "top" ), "top" ), i;
	int menu = 0, common = -1;

	if ( isconfig ) {
		for ( i = 0; i < ned; i++ ) {
			if ( !strnicmp( ed[i], "[menu]", 6 ) )
				menu = 1;
			if ( !strnicmp( ed[i], "[common]", 8 ) )
				common = i;
		}
	}
	if ( menu ) {
		/* end of the last [common] block, or a new one at the end */
		if ( common < 0 ) {
			ed_insert( ned, "[common]" );
			at = ned;
		} else {
			for ( at = common + 1; at < ned && ed[at][0] != '['; at++ );
			if ( top )
				at = common + 1;
		}
	} else if ( top ) {
		at = 0;
		while ( at < ned && ( !strnicmp( ed[at], "@ECHO", 5 ) ) )
			at++;
	} else {
		at = ned;
		if ( !isconfig )
			for ( i = 0; i < ned; i++ ) {
				const char *s = ed[i], *b;
				while ( *s == ' ' || *s == '@' )
					s++;
				b = strrchr( s, '\\' );
				b = b ? b + 1 : s;
				if ( ( !strnicmp( b, "WIN", 3 ) && ( !b[3] || b[3] == ' ' || b[3] == '.' ) ) ) {
					at = i;
					break;
				}
			}
	}
	while ( ( pos = ini_next( section, "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			ed_insert( at++, line );
		}
}

/* SYSTEM.INI: Add=cond|Section|line */
static void ed_add_ini( void )
{
	const char *v, *t;
	char sec[40], line[256], hdr[44];
	int pos = 0, i;
	while ( ( pos = ini_next( "System.ini", "Add", pos, &v ) ) >= 0 ) {
		const char *bar;
		if ( !( t = cond( v ) ) || !( bar = strchr( t, '|' ) ) )
			continue;
		sprintf( sec, "%.*s", (int)( bar - t ), t );
		expand( line, bar + 1, sizeof(line) );
		sprintf( hdr, "[%s]", sec );
		for ( i = 0; i < ned && strnicmp( ed[i], hdr, strlen( hdr ) ); i++ );
		if ( i == ned ) {
			ed_insert( ned, "" );
			ed_insert( ned, hdr );
			i = ned - 1;
		}
		ed_insert( i + 1, line );
	}
}

static int has_active( const char *section, const char *key )
{
	const char *v;
	int pos = 0;
	while ( ( pos = ini_next( section, key, pos, &v ) ) >= 0 )
		if ( !strcmp( key, "Remove" ) || cond( v ) )
			return 1;
	return 0;
}

static void edit_file( const char *path, const char *section, int kind )
{
	if ( !has_active( section, "Add" ) && !has_active( section, "Remove" ) )
		return;
	if ( ed_load( path ) && uninstall )
		return;
	ed_remove( section );
	if ( !uninstall ) {
		if ( kind == 2 )
			ed_add_ini();
		else
			ed_add_dos( section, kind == 0 );
	}
	if ( ed_save( path ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "WriteError", "Cannot write" ), path );
		fail( msg );
	}
	ed_free();
}

/* --- setup steps ------------------------------------------------------ */

static void find_windows( void )
{
	const char *path = getenv( "PATH" );
	char p[80], dir[80];
	if ( path )
		while ( *path ) {
			int n = 0;
			while ( path[n] && path[n] != ';' )
				n++;
			sprintf( dir, "%.*s", n, path );
			path_cat( p, dir, "WIN.COM" );
			if ( n && file_exists( p ) ) {
				strcpy( windir, dir );
				return;
			}
			path += n;
			if ( *path )
				path++;
		}
	sprintf( p, "%s\\WINDOWS\\WIN.COM", boot );
	if ( file_exists( p ) )
		sprintf( windir, "%s\\WINDOWS", boot );
}

static void show_text( const char *title, const char *file, const char **buttons, int n, int must )
{
	char p[80], **l;
	int nl, rc;
	path_cat( p, srcdir, file );
	if ( !( l = text_load( p, &nl ) ) ) {
		char msg[120];
		sprintf( msg, "%s %s", T( "MissingFile", "File not found:" ), p );
		fail( msg );
	}
	scr_keys( T( "KeysView", "PgUp=Page up  PgDn=Page down  Enter=Select  Esc=Abort" ) );
	rc = scr_view( title, l, nl, buttons, n, 0 );
	if ( must && rc != 0 )
		abort_setup();
}

static void welcome( void )
{
	const char *v;
	static char text[1200];
	static const char *b[2];
	int pos = 0;
	b[0] = T( "Continue", "Continue" );
	b[1] = T( "Exit", "Exit" );
	text[0] = 0;
	while ( ( pos = ini_lines( "Welcome", pos, &v ) ) >= 0 && strlen( text ) + strlen( v ) < sizeof(text) - 2 ) {
		strcat( text, v );
		strcat( text, "\n" );
	}
	if ( !text[0] )
		sprintf( text, "%s %s.", T( "WelcomeText", "This program installs" ), ini_get( "Setup", "Product", "the software" ) );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	if ( scr_message( T( "WelcomeTitle", "Welcome" ), text, b, 2, 0, 0 ) != 0 )
		abort_setup();
}

static void read_options( void )
{
	const char *v;
	int pos = 0;
	while ( nopt < MAXOPT && ( pos = ini_next( "Options", "Option", pos, &v ) ) >= 0 ) {
		/* Option=on|Label|value: %On% expands to value if the option is on */
		const char *bar = strchr( v, '|' ), *bar2;
		static char labels[MAXOPT][70];
		opt_on[nopt] = !strnicmp( v, "on", 2 ) || *v == '1' || toupper( *v ) == 'Y';
		bar = bar ? bar + 1 : v;
		bar2 = strchr( bar, '|' );
		sprintf( labels[nopt], "%.*s", bar2 ? (int)( bar2 - bar ) : 69, bar );
		if ( bar2 )
			sprintf( opt_value[nopt], "%.23s", bar2 + 1 );
		opt_label[nopt] = labels[nopt];
		nopt++;
	}
}

static void ask_options( void )
{
	if ( !nopt )
		return;
	scr_keys( T( "KeysOpt", "Space=On/off  Enter=Continue  Esc=Abort" ) );
	if ( scr_checklist( T( "OptionsTitle", "Options" ),
		T( "OptionsText", "Select the parts to install (Space switches on/off):" ), opt_label, opt_on, nopt ) )
		abort_setup();
}

static void ask_dirs( void )
{
	strcpy( dest, ini_get( "Setup", "DefaultDir", "C:\\PROGRAM" ) );
	expand( dest, dest, sizeof(dest) );
	scr_keys( T( "KeysInput", "Enter=Continue  Esc=Abort" ) );
	for ( ;; ) {
		if ( scr_input( T( "DirTitle", "Destination" ), T( "DirText", "Install the files to this directory:" ), dest, 64 ) )
			abort_setup();
		if ( strlen( dest ) >= 3 && dest[1] == ':' && dest[2] == '\\' && !mkdirs( dest ) )
			break;
		{
			static const char *ok[] = { "OK" };
			scr_message( T( "ErrorTitle", "Error" ), T( "DirError", "Please enter a full path (e.g. C:\\DRIVERS) that can be created." ), ok, 1, 0, 1 );
		}
	}
	if ( has_active( "System.ini", "Add" ) ) {
		find_windows();
		if ( scr_input( T( "WinTitle", "Windows" ),
			T( "WinText", "Windows directory for the SYSTEM.INI entries (empty: no Windows):" ), windir, 64 ) )
			abort_setup();
	}
}

static int confirm( void )
{
	static char text[1500];
	static const char *b[2];
	const char *v, *t;
	char line[160];
	int pos;
	b[0] = T( "Install", "Install" );
	b[1] = T( "Cancel", "Cancel" );
	sprintf( text, "%s %s\n", T( "SumFiles", "Files to:" ), dest );
	pos = 0;
	while ( ( pos = ini_next( "Config.sys", "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			sprintf( text + strlen( text ), "%s\\CONFIG.SYS: %s\n", boot, line );
		}
	pos = 0;
	while ( ( pos = ini_next( "Autoexec.bat", "Add", pos, &v ) ) >= 0 )
		if ( ( t = cond( v ) ) != NULL ) {
			expand( line, t, sizeof(line) );
			sprintf( text + strlen( text ), "%s\\AUTOEXEC.BAT: %s\n", boot, line );
		}
	if ( windir[0] ) {
		pos = 0;
		while ( ( pos = ini_next( "System.ini", "Add", pos, &v ) ) >= 0 )
			if ( ( t = cond( v ) ) != NULL && strchr( t, '|' ) ) {
				expand( line, strchr( t, '|' ) + 1, sizeof(line) );
				sprintf( text + strlen( text ), "SYSTEM.INI [%.*s]: %s\n", (int)( strchr( t, '|' ) - t ), t, line );
			}
	}
	strcat( text, T( "SumBackup", "\nThe original files are kept as *.G01, *.G02, ..." ) );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	return scr_message( T( "SumTitle", "Ready to install" ), text, b, 2, 0, 0 ) == 0;
}

static void copy_all( void )
{
	const char *v, *t;
	char from[80], to[80], name[40];
	int pos = 0, total = 0, done = 0;
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 )
		if ( cond( v ) )
			total++;
	if ( !stricmp( ini_get( "Setup", "Uninstall", "yes" ), "yes" ) )
		total += 2;
	pos = 0;
	scr_progress( T( "CopyTitle", "Copying files" ), "", 0, total );
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
		const char *eq;
		if ( !( t = cond( v ) ) )
			continue;
		eq = strchr( t, '=' );   /* SOURCE=DESTNAME */
		sprintf( name, "%.*s", eq ? (int)( eq - t ) : 39, t );
		path_cat( from, srcdir, name );
		path_cat( to, dest, eq ? eq + 1 : name );
		scr_progress( T( "CopyTitle", "Copying files" ), name, ++done, total );
		if ( copy_file( from, to ) ) {
			char msg[160];
			sprintf( msg, "%s %s -> %s", T( "CopyError", "Cannot copy" ), from, to );
			fail( msg );
		}
	}
	if ( !stricmp( ini_get( "Setup", "Uninstall", "yes" ), "yes" ) ) {
		/* INSTALL /U in the target directory removes the installation */
		path_cat( from, srcdir, "INSTALL.EXE" );
		path_cat( to, dest, "INSTALL.EXE" );
		scr_progress( T( "CopyTitle", "Copying files" ), "INSTALL.EXE", ++done, total );
		copy_file( from, to );
		path_cat( from, srcdir, "INSTALL.INI" );
		path_cat( to, dest, "INSTALL.INI" );
		scr_progress( T( "CopyTitle", "Copying files" ), "INSTALL.INI", ++done, total );
		copy_file( from, to );
	}
}

static void edit_all( void )
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
	}
}

static void remove_all( void )
{
	const char *v, *t;
	char p[80], name[40];
	int pos = 0;
	static const char *b[2];
	b[0] = T( "Remove", "Remove" );
	b[1] = T( "Cancel", "Cancel" );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	if ( scr_message( T( "UninstTitle", "Remove" ), T( "UninstText", "Remove the installation from this directory and its entries from CONFIG.SYS, AUTOEXEC.BAT and SYSTEM.INI?" ), b, 2, 1, 0 ) != 0 )
		abort_setup();
	strcpy( dest, srcdir );
	for ( pos = 0; pos < MAXOPT; pos++ )
		opt_on[pos] = 1;
	find_windows();
	edit_all();
	pos = 0;
	while ( ( pos = ini_lines( "Files", pos, &v ) ) >= 0 ) {
		const char *eq;
		t = strchr( v, '|' ) ? strchr( v, '|' ) + 1 : v;
		eq = strchr( t, '=' );
		strcpy( name, eq ? eq + 1 : t );
		path_cat( p, dest, name );
		remove( p );
	}
	path_cat( p, dest, "INSTALL.INI" );
	remove( p );
	path_cat( p, dest, "INSTALL.EXE" );
	remove( p );
	rmdir( dest );
}

static void finish( void )
{
	static const char *b[2];
	const char *readme = ini_get( "Setup", "Readme", NULL );
	int rc;
	b[0] = T( "OK", "OK" );
	b[1] = T( "ShowReadme", "Readme" );
	scr_keys( T( "KeysMsg", "Enter=Select  Esc=Abort" ) );
	rc = scr_message( T( "DoneTitle", "Setup complete" ),
		uninstall ? T( "UninstDone", "The installation has been removed. Restart the computer to unload the drivers." )
		          : ini_get( "Setup", "Done", T( "DoneText", "The installation is complete. Restart the computer to load the drivers." ) ),
		b, readme && !uninstall ? 2 : 1, 0, 0 );
	if ( rc == 1 ) {
		static const char *ok[1];
		ok[0] = T( "OK", "OK" );
		show_text( T( "ReadmeTitle", "Readme" ), readme, ok, 1, 0 );
	}
}

static void usage( void )
{
	printf( "GeosOne DOS Installer " GDI_VERSION " - GNU GPL version 3\n"
	        "INSTALL [/U] [file.INI]\n"
	        "  installs as described by INSTALL.INI next to INSTALL.EXE\n"
	        "  /U  removes the installation (run INSTALL.EXE in the target directory)\n" );
}

int main( int argc, char **argv )
{
	char ini[80], *s;
	union REGS r;
	int i;

	strcpy( srcdir, argv[0] );
	if ( ( s = strrchr( srcdir, '\\' ) ) != NULL )
		*s = 0;
	else
		getcwd( srcdir, sizeof(srcdir) );
	path_cat( ini, srcdir, "INSTALL.INI" );
	for ( i = 1; i < argc; i++ ) {
		if ( argv[i][0] == '/' || argv[i][0] == '-' ) {
			if ( toupper( argv[i][1] ) == 'U' )
				uninstall = 1;
			else {
				usage();
				return 0;
			}
		} else
			strcpy( ini, argv[i] );
	}
	if ( ini_load( ini ) ) {
		printf( "INSTALL: %s not found\n", ini );
		return 2;
	}
	r.x.ax = 0x3305;   /* boot drive */
	intdos( &r, &r );
	if ( r.h.dl >= 1 && r.h.dl <= 26 )
		boot[0] = 'A' + r.h.dl - 1;

	scr_init();
	scr_bars( ini_get( "Setup", "Title", "Setup" ), ini_get( "Setup", "Product", "GeosOne DOS Installer" ), "" );
	scr_desktop();
	read_options();
	if ( uninstall )
		remove_all();
	else {
		welcome();
		if ( ini_get( "Setup", "License", NULL ) ) {
			static const char *b[2];
			b[0] = T( "Accept", "I accept" );
			b[1] = T( "Decline", "Decline" );
			show_text( T( "LicenseTitle", "License agreement" ), ini_get( "Setup", "License", NULL ), b, 2, 1 );
			scr_desktop();
		}
		ask_dirs();
		scr_desktop();
		ask_options();
		scr_desktop();
		if ( !confirm() )
			abort_setup();
		scr_desktop();
		copy_all();
		edit_all();
	}
	scr_desktop();
	finish();
	scr_exit();
	return 0;
}
