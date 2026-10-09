/* screen.c - text mode user interface of the GeosOne DOS Installer:
 * 80x25 color text screen written directly to video memory, windows with a
 * title in the frame and a shadow, buttons, a line editor and a text viewer
 * (the look of the Qualitas MAX installer).
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <string.h>
#include <i86.h>
#include "gdi.h"

#define COLS 80
#define ROWS 25

static unsigned short far *vram;

/* colors */
#define A_DESK    0x11   /* desktop: blue */
#define A_TOPL    0x0F   /* top bar left: white on black */
#define A_TOPR    0x1F   /* top bar right: white on blue */
#define A_BOTTOM  0x70   /* key bar */
#define A_BOTKEY  0x74   /* key in the key bar */
#define A_WIN     0x3F   /* window: white on cyan */
#define A_WINTXT  0x30   /* window text: black on cyan */
#define A_HILITE  0x3E   /* yellow on cyan */
#define A_BUTTON  0x70   /* button */
#define A_BUTSEL  0x1F   /* selected button */
#define A_FIELD   0x1F   /* input field */
#define A_SHADOW  0x08
#define A_ERROR   0x4F   /* error window: white on red */

static const char *topleft = "", *topright = "";

void scr_init( void )
{
	union REGS r;
	r.h.ah = 0x0F;
	int86( 0x10, &r, &r );
	vram = ( r.h.al == 7 ) ? MK_FP( 0xB000, 0 ) : MK_FP( 0xB800, 0 );
	if ( r.h.al != 3 && r.h.al != 7 ) {
		r.x.ax = 0x0003;
		int86( 0x10, &r, &r );
	}
	r.x.ax = 0x1003;   /* bright backgrounds, no blinking */
	r.h.bl = 0;
	int86( 0x10, &r, &r );
	scr_cursor( -1, -1 );
}

void scr_exit( void )
{
	union REGS r;
	scr_fill( 0, 0, COLS, ROWS, ' ', 0x07 );
	r.h.ah = 2; r.h.bh = 0; r.x.dx = 0;
	int86( 0x10, &r, &r );
	r.h.ah = 1; r.x.cx = 0x0607;
	int86( 0x10, &r, &r );
}

void scr_cursor( int x, int y )
{
	union REGS r;
	r.h.ah = 1;
	r.x.cx = ( x < 0 ) ? 0x2000 : 0x0607;
	int86( 0x10, &r, &r );
	if ( x >= 0 ) {
		r.h.ah = 2; r.h.bh = 0; r.h.dh = y; r.h.dl = x;
		int86( 0x10, &r, &r );
	}
}

int scr_key( void )
{
	union REGS r;
	r.h.ah = 0;
	int86( 0x16, &r, &r );
	return r.h.al ? r.h.al : 0x100 | r.h.ah;   /* 0x1xx: scan code */
}

void scr_put( int x, int y, int attr, const char *s )
{
	for ( ; *s && x < COLS; s++, x++ )
		vram[y * COLS + x] = ( attr << 8 ) | (unsigned char)*s;
}

void scr_putn( int x, int y, int attr, const char *s, int n )
{
	for ( ; n > 0 && x < COLS; n--, x++ )
		vram[y * COLS + x] = ( attr << 8 ) | (unsigned char)( *s ? *s++ : ' ' );
}

void scr_fill( int x, int y, int w, int h, int ch, int attr )
{
	int i, j;
	for ( j = y; j < y + h && j < ROWS; j++ )
		for ( i = x; i < x + w && i < COLS; i++ )
			vram[j * COLS + i] = ( attr << 8 ) | (unsigned char)ch;
}

/* shadow: dark gray text, the desktop pattern becomes black */
static void shade( int i )
{
	unsigned c = vram[i] & 0xFF;
	vram[i] = ( A_SHADOW << 8 ) | ( c == 0xB0 ? ' ' : c );
}

static void shadow( int x, int y, int w, int h )
{
	int i;
	for ( i = y + 1; i <= y + h && i < ROWS; i++ ) {
		if ( x + w < COLS )
			shade( i * COLS + x + w );
		if ( x + w + 1 < COLS )
			shade( i * COLS + x + w + 1 );
	}
	if ( y + h < ROWS )
		for ( i = x + 2; i < x + w + 2 && i < COLS; i++ )
			shade( ( y + h ) * COLS + i );
}

/* window with a double line frame, the title in the top line */
void scr_window( int x, int y, int w, int h, int attr, const char *title )
{
	int i, tl = strlen( title );
	scr_fill( x, y, w, h, ' ', attr );
	for ( i = x + 1; i < x + w - 1; i++ ) {
		vram[y * COLS + i] = ( attr << 8 ) | 0xCD;
		vram[( y + h - 1 ) * COLS + i] = ( attr << 8 ) | 0xCD;
	}
	for ( i = y + 1; i < y + h - 1; i++ ) {
		vram[i * COLS + x] = ( attr << 8 ) | 0xBA;
		vram[i * COLS + x + w - 1] = ( attr << 8 ) | 0xBA;
	}
	vram[y * COLS + x] = ( attr << 8 ) | 0xC9;
	vram[y * COLS + x + w - 1] = ( attr << 8 ) | 0xBB;
	vram[( y + h - 1 ) * COLS + x] = ( attr << 8 ) | 0xC8;
	vram[( y + h - 1 ) * COLS + x + w - 1] = ( attr << 8 ) | 0xBC;
	if ( tl ) {
		scr_put( x + ( w - tl - 2 ) / 2, y, attr, " " );
		scr_put( x + ( w - tl - 2 ) / 2 + 1, y, attr, title );
		scr_put( x + ( w - tl - 2 ) / 2 + 1 + tl, y, attr, " " );
	}
	shadow( x, y, w, h );
}

void scr_bars( const char *left, const char *right, const char *keys )
{
	topleft = left;
	topright = right;
	scr_fill( 0, 0, COLS, 1, ' ', A_TOPL );
	scr_put( 1, 0, A_TOPL, left );
	scr_fill( COLS - strlen( right ) - 2, 0, strlen( right ) + 2, 1, ' ', A_TOPR );
	scr_put( COLS - strlen( right ) - 1, 0, A_TOPR, right );
	scr_keys( keys );
}

/* key bar: "Esc=Abort  Enter=Continue": the key in red */
void scr_keys( const char *keys )
{
	int x = 1;
	scr_fill( 0, ROWS - 1, COLS, 1, ' ', A_BOTTOM );
	while ( *keys && x < COLS ) {
		const char *eq = strchr( keys, '=' ), *sp;
		if ( !eq )
			break;
		sp = strstr( eq, "  " );
		if ( !sp )
			sp = eq + strlen( eq );
		scr_putn( x, ROWS - 1, A_BOTTOM, eq + 1, sp - eq - 1 );
		x += sp - eq - 1 + 1;
		scr_putn( x, ROWS - 1, A_BOTKEY, keys, eq - keys );
		x += eq - keys + 3;
		keys = *sp ? sp + 2 : sp;
	}
}

void scr_desktop( void )
{
	scr_fill( 0, 1, COLS, ROWS - 2, 0xB0, A_DESK );
}

/* word-wrapped text in a window area; '\n' breaks lines; returns lines */
int scr_text( int x, int y, int w, int h, int attr, const char *s, int draw )
{
	int line = 0;
	while ( *s ) {
		int n = 0, brk = -1;
		while ( s[n] && s[n] != '\n' && n < w ) {
			if ( s[n] == ' ' )
				brk = n;
			n++;
		}
		if ( s[n] && s[n] != '\n' && brk > 0 )
			n = brk;
		if ( draw && line < h )
			scr_putn( x, y + line, attr, s, n );
		line++;
		s += n;
		if ( *s == '\n' || *s == ' ' )
			s++;
	}
	return line;
}

/* buttons centered in row y; sel = selected; returns the chosen index,
 * -1 for Esc.  Keys: Tab/arrows, Enter, the first letter of a button */
int scr_buttons( int y, int x, int w, const char **labels, int n, int sel )
{
	int i, k, pos[8];
	for ( ;; ) {
		int total = 0, cx;
		for ( i = 0; i < n; i++ )
			total += strlen( labels[i] ) + 4 + 2;
		cx = x + ( w - total + 2 ) / 2;
		for ( i = 0; i < n; i++ ) {
			char b[40];
			int l = strlen( labels[i] );
			pos[i] = cx;
			b[0] = ' ';
			b[1] = i == sel ? 0xAE : ' ';
			memcpy( b + 2, labels[i], l );
			b[l + 2] = i == sel ? 0xAF : ' ';
			b[l + 3] = ' ';
			b[l + 4] = 0;
			scr_put( cx, y, i == sel ? A_BUTSEL : A_BUTTON, b );
			scr_fill( cx + 1, y + 1, l + 4, 1, 0xDF, ( vram[( y + 1 ) * COLS + cx] & 0xF000 ) >> 8 );
			vram[y * COLS + cx + l + 4] = ( ( vram[y * COLS + cx + l + 4] & 0xF000 ) ) | 0xDC;
			cx += l + 4 + 2;
		}
		(void)pos;
		k = scr_key();
		if ( k == 27 )
			return -1;
		if ( k == 13 )
			return sel;
		if ( k == 9 || k == 0x14D || k == 0x150 )
			sel = ( sel + 1 ) % n;
		else if ( k == 0x10F || k == 0x14B || k == 0x148 )
			sel = ( sel + n - 1 ) % n;
		else
			for ( i = 0; i < n; i++ )
				if ( k < 0x100 && ( k | 0x20 ) == ( labels[i][0] | 0x20 ) )
					return i;
	}
}

/* message window with buttons; returns the chosen button or -1 */
int scr_message( const char *title, const char *text, const char **buttons, int n, int sel, int error )
{
	int w = 60, lines = scr_text( 0, 0, w - 4, 99, 0, text, 0 );
	int h = lines + 6, x = ( COLS - w ) / 2, y = ( ROWS - h ) / 2;
	int attr = error ? A_ERROR : A_WIN;
	scr_window( x, y, w, h, attr, title );
	scr_text( x + 2, y + 2, w - 4, lines, error ? attr : A_WINTXT, text, 1 );
	return scr_buttons( y + h - 3, x, w, buttons, n, sel );
}

/* line editor in a window; returns 0 = Enter, -1 = Esc */
int scr_input( const char *title, const char *text, char *buf, int max )
{
	int w = 64, lines = scr_text( 0, 0, w - 4, 99, 0, text, 0 );
	int h = lines + 6, x = ( COLS - w ) / 2, y = ( ROWS - h ) / 2;
	int fx = x + 2, fy = y + 2 + lines + 1, fw = w - 4, len = strlen( buf ), k;
	scr_window( x, y, w, h, A_WIN, title );
	scr_text( x + 2, y + 2, w - 4, lines, A_WINTXT, text, 1 );
	for ( ;; ) {
		scr_fill( fx, fy, fw, 1, ' ', A_FIELD );
		scr_put( fx, fy, A_FIELD, buf );
		scr_cursor( fx + len, fy );
		k = scr_key();
		if ( k == 13 ) {
			scr_cursor( -1, -1 );
			return 0;
		}
		if ( k == 27 ) {
			scr_cursor( -1, -1 );
			return -1;
		}
		if ( k == 8 && len > 0 )
			buf[--len] = 0;
		else if ( k >= 32 && k < 256 && len < max && len < fw - 1 ) {
			buf[len++] = (char)( k >= 'a' && k <= 'z' ? k - 32 : k );
			buf[len] = 0;
		}
	}
}

/* text viewer (license, readme); returns the button chosen or -1 */
int scr_view( const char *title, char **lines, int nlines, const char **buttons, int n, int sel )
{
	int x = 2, y = 2, w = COLS - 4, h = ROWS - 4, top = 0, k, i;
	int vis = h - 5;
	scr_window( x, y, w, h, A_WIN, title );
	for ( ;; ) {
		for ( i = 0; i < vis; i++ )
			scr_putn( x + 2, y + 1 + i, A_WINTXT, top + i < nlines ? lines[top + i] : "", w - 4 );
		{
			char pos[24];
			int pct = nlines > vis ? (int)( ( (long)( top + vis ) * 100 ) / nlines ) : 100;
			if ( pct > 100 )
				pct = 100;
			pos[0] = ' ';
			itoa_dec( pct, pos + 1 );
			strcat( pos, "% " );
			scr_put( x + w - 8, y + h - 4, A_WIN, "\xCD\xCD\xCD\xCD\xCD\xCD" );
			scr_put( x + w - 2 - strlen( pos ), y + h - 4, A_HILITE, pos );
		}
		/* buttons always visible */
		{
			int total = 0, cx;
			for ( i = 0; i < n; i++ )
				total += strlen( buttons[i] ) + 6;
			cx = x + ( w - total + 2 ) / 2;
			for ( i = 0; i < n; i++ ) {
				scr_put( cx, y + h - 3, i == sel ? A_BUTSEL : A_BUTTON, "  " );
				scr_put( cx + 2, y + h - 3, i == sel ? A_BUTSEL : A_BUTTON, buttons[i] );
				scr_put( cx + 2 + strlen( buttons[i] ), y + h - 3, i == sel ? A_BUTSEL : A_BUTTON, "  " );
				cx += strlen( buttons[i] ) + 6;
			}
		}
		k = scr_key();
		switch ( k ) {
		case 0x148: if ( top > 0 ) top--; break;                         /* up */
		case 0x150: if ( top + vis < nlines ) top++; break;              /* down */
		case 0x149: top = top > vis ? top - vis : 0; break;             /* PgUp */
		case 0x151: if ( top + vis < nlines ) top += vis;               /* PgDn */
		            if ( top + vis > nlines && nlines > vis ) top = nlines - vis; break;
		case 0x147: top = 0; break;                                     /* Home */
		case 0x14F: top = nlines > vis ? nlines - vis : 0; break;       /* End */
		case 27: return -1;
		default:
			if ( k == 13 || k == 9 || ( k < 0x100 && k > 32 ) ) {
				for ( i = 0; i < n; i++ )
					if ( k < 0x100 && k > 32 && ( k | 0x20 ) == ( buttons[i][0] | 0x20 ) )
						return i;
				return scr_buttons( y + h - 3, x, w, buttons, n, sel );
			}
		}

	}
}

/* checkbox list: on[] toggled with Space; returns 0 = Enter, -1 = Esc */
int scr_checklist( const char *title, const char *text, const char **items, char *on, int n )
{
	int w = 64, lines = scr_text( 0, 0, w - 4, 99, 0, text, 0 );
	int h = lines + n + 5, x = ( COLS - w ) / 2, y = ( ROWS - h ) / 2, cur = 0, i, k;
	scr_window( x, y, w, h, A_WIN, title );
	scr_text( x + 2, y + 2, w - 4, lines, A_WINTXT, text, 1 );
	for ( ;; ) {
		for ( i = 0; i < n; i++ ) {
			int a = i == cur ? A_BUTSEL : A_WINTXT;
			scr_fill( x + 2, y + 3 + lines + i, w - 4, 1, ' ', a );
			scr_put( x + 3, y + 3 + lines + i, a, on[i] ? "[X] " : "[ ] " );
			scr_putn( x + 7, y + 3 + lines + i, a, items[i], w - 10 );
		}
		k = scr_key();
		if ( k == 13 )
			return 0;
		if ( k == 27 )
			return -1;
		if ( k == ' ' )
			on[cur] = !on[cur];
		else if ( k == 0x148 && cur > 0 )
			cur--;
		else if ( k == 0x150 && cur < n - 1 )
			cur++;
	}
}

/* progress window: call with done = 0 first */
void scr_progress( const char *title, const char *text, long done, long total )
{
	int w = 60, h = 7, x = ( COLS - w ) / 2, y = ( ROWS - h ) / 2, bw = w - 6, n;
	if ( !done )
		scr_window( x, y, w, h, A_WIN, title );
	scr_fill( x + 2, y + 2, w - 4, 1, ' ', A_WINTXT );
	scr_putn( x + 2, y + 2, A_WINTXT, text, w - 4 );
	n = total ? (int)( done * bw / total ) : 0;
	scr_fill( x + 3, y + 4, bw, 1, 0xB0, A_WINTXT );
	scr_fill( x + 3, y + 4, n, 1, 0xDB, A_HILITE );
}

void itoa_dec( int v, char *s )
{
	char t[8];
	int i = 0;
	do {
		t[i++] = '0' + v % 10;
		v /= 10;
	} while ( v );
	while ( i )
		*s++ = t[--i];
	*s = 0;
}
