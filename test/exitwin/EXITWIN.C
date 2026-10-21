/* EXITWIN seconds [program]: starts the program, then ends Windows after
 * that many seconds, so that the Windows tests shut Windows down cleanly (WfW
 * 3.11 caches disk writes and WIN.INI until Windows ends). */
#include <windows.h>

int PASCAL WinMain( HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show )
{
    MSG message;
    int seconds = 0;
    LPSTR program = command_line;
    (void)instance; (void)previous;
    while ( *program >= '0' && *program <= '9' )
        seconds = seconds * 10 + *program++ - '0';
    while ( *program == ' ' )
        program++;
    if ( *program )
        WinExec( program, show );
    if ( seconds < 1 )
        seconds = 30;
    SetTimer( NULL, 1, (UINT)seconds * 1000, NULL );
    while ( GetMessage( &message, NULL, 0, 0 ) ) {
        if ( message.message == WM_TIMER ) {
            /* again after 5 seconds if an application refused */
            ExitWindows( 0, 0 );
            SetTimer( NULL, 1, 5000, NULL );
            continue;
        }
        TranslateMessage( &message );
        DispatchMessage( &message );
    }
    return 0;
}
