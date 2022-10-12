//
// Update Delay: shows the delta in time since the last update to the view of a given app
//

#include <windows.h>
#include <shlwapi.h>

#include <stdio.h>

#include <memory>
#include <string>
#include <regex>
#include <vector>
#include <chrono>

#include <djl_wait.hxx>

using namespace std;
using namespace std::chrono;

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "kernel32.lib" )
#pragma comment( lib, "gdi32.lib" )

void Usage( const char * pcMessage = 0 )
{
    if ( 0 != pcMessage )
        printf( "error: %s\n", pcMessage );

    printf( "usage: ud [-v] [-w] [appname]\n" );
    printf( "  Update Delay: shows the milliseconds since the prior update to a window\n" );
    printf( "  arguments: [appname]       Name of the app to capture. Can contain wildcards.\n" );
    printf( "             [-v]            Verbose logging of debugging information.\n" );
    printf( "             [-w]            Capture the whole window, not just the client area.\n" );
    printf( "  sample usage: (arguments can use - or /)\n" );
    printf( "    ud inbox*outlook\n" );
    printf( "    ud *excel\n" );
    printf( "    ud -w calc*\n" );
    printf( "    ud 0x6bf0\n" );
    printf( "    ud                       Shows titles and positions of visible top-level windows\n" );
    printf( "  notes:\n" );
    printf( "    - if no arguments are specified then all window positions are printed\n" );
    printf( "    - appname can contain ? and * characters as wildcards\n" );
    printf( "    - appname is case insensitive\n" );
    printf( "    - appname can alternatively be a window handle or process id in decimal or hex\n" );
    printf( "    - only visible, top-level windows are considered\n" );
    printf( "    - the screen is grabbed, so obscurring windows are considered part of the app itself\n" );
    printf( "    - extra pixels surrounding windows exist because Windows draws narrow inner borders\n" );
    printf( "      and transparent outer edges for borders that are in fact wide\n" );
    printf( "    - 'modern' app client area includes the chrome\n" );
    printf( "    - doesn't work when a screen saver or lock screen appears\n" );
    printf( "    - Only accurate within perhaps 1/5 of a second; it's not precise.\n" );
    exit( 0 );
} //Usage

bool g_Enumerate = true;                                        // enumerate windows; don't capture bitmap
bool g_WholeWindow = false;                                     // the whole window, not just the client area
bool g_Verbose = false;                                         // print debug information
bool g_FoundMatchingWindow = false;                             // did we find a matching window?
WCHAR g_AppName[ MAX_PATH ] = {0};                              // name of the app to find, can contain wildcards
WCHAR g_AppNameRegex[ 3 * _countof( g_AppName ) ] = {0};        // regex equivalent of g_AppName
unsigned long long g_AppId = 0;                                 // hwnd or procid to use instead of g_AppName

bool HasBitmapChanged( HBITMAP hbLatest, HDC hdcMem )
{
    static HBITMAP hbPrior = 0;
    bool identical = false;

    if ( 0 != hbPrior )
    {
        BITMAPINFO biPrior = {0};
        biPrior.bmiHeader.biSize = sizeof biPrior.bmiHeader;
        int response = GetDIBits( hdcMem, hbPrior, 0, 0, 0, &biPrior, DIB_RGB_COLORS );
        if ( 0 == response )
        {
            printf( "can't get prior's GetDIBits: %d\n", GetLastError() );
            DeleteObject( hbLatest );
            return false;
        }

        BITMAPINFO biLatest = {0};
        biLatest.bmiHeader.biSize = sizeof biLatest.bmiHeader;
        response = GetDIBits( hdcMem, hbLatest, 0, 0, 0, &biLatest, DIB_RGB_COLORS );

        if ( response )
        {
            if ( 0 == memcmp( &biLatest, &biPrior, sizeof biLatest ) )
            {
                // printf( "compression: %d, bi_rgb %d\n", biLatest.bmiHeader.biCompression, BI_RGB );
                // Ensure we get RGB out. The default we get back is BI_BITFIELDS
                // With BI_BITFIELDS, 3 DWORDs are written to the BITMAPINFO, trashing the stack.

                biLatest.bmiHeader.biCompression = BI_RGB;
                biPrior.bmiHeader.biCompression = BI_RGB;

                int height = abs( biLatest.bmiHeader.biHeight );
                size_t extent = ( biLatest.bmiHeader.biWidth * height );
                vector<DWORD> priorbits( extent );
                vector<DWORD> latestbits( extent );

                if ( extent * 4 != biPrior.bmiHeader.biSizeImage )
                    printf( "difference!\n" );

                response = GetDIBits( hdcMem, hbLatest, 0, height, latestbits.data(), &biLatest, DIB_RGB_COLORS );
                if ( response != height )
                    printf( "unexpected response %d from GetDIBits for latest bitmap\n", response );

                response = GetDIBits( hdcMem, hbPrior, 0, height, priorbits.data(), &biPrior, DIB_RGB_COLORS );
                if ( response != height )
                    printf( "unexpected response %d from GetDIBits for prior bitmap\n", response );

                identical = true;
                for ( size_t x = 0; x < extent; x++ )
                {
                    //printf( "%#x, ", priorbits[ x ] );
                    if ( priorbits[ x ] != latestbits[ x ] )
                    {
                        identical = false;
                        break;
                    }
                }
            }
        }
        else
            printf( "can't getDIBits for latest bitmap: %d\n", GetLastError() );

        if ( identical )
        {
            DeleteObject( hbLatest );
        }
        else
        {
            if ( 0 != hbPrior )
                DeleteObject( hbPrior );

            hbPrior = hbLatest;
        }
    }
    else
    {
        hbPrior = hbLatest;
        identical = true;
    }

    return !identical;
} //CompareBitmaps   

high_resolution_clock::time_point tPrior;

BOOL CALLBACK EnumWindowsProc( HWND hwnd, LPARAM lParam )
{
    BOOL visible = IsWindowVisible( hwnd );
    if ( !visible )
        return true;

    static WCHAR awcText[ 1000 ];
    int len = GetWindowText( hwnd, awcText, _countof( awcText ) );
    if ( 0 == len )
    {
        //printf( "can't retrieve window text, error %d\n", GetLastError() );
        return true;
    }

    // Ignore the console window in which this app is running

    static WCHAR awcConsole[ 1000 ];
    DWORD consoleTitleLen = GetConsoleTitle( awcConsole, _countof( awcConsole ) );
    if ( ( 0 != consoleTitleLen ) && !wcscmp( awcConsole, awcText ) )
        return true;

    // 0x2000 - 0x206f is punctuation, which printf handles badly. convert to spaces

    for ( int i = 0; i < len; i++ )
        if ( awcText[ i ] >= 0x2000 && awcText[ i ] <= 0x206f )
            awcText[ i ] = ' ';

    DWORD procId = 0;
    GetWindowThreadProcessId( hwnd, & procId );

    WINDOWPLACEMENT wp;
    wp.length = sizeof wp;
    BOOL ok = GetWindowPlacement( hwnd, & wp );
    if ( !ok )
        return true;

    int ret = 0;
    WINDOWINFO wi;
    wi.cbSize = sizeof wi;
    ok = GetWindowInfo( hwnd, & wi );
    if ( ok && ( ( SW_NORMAL == wp.showCmd ) || ( SW_MAXIMIZE == wp.showCmd ) ) )
    {
        RECT rcWindow = wi.rcWindow;

        if ( g_Enumerate )
        {
            printf( " %6d %6d %6d %6d %#10llx %#5d '%ws'\n",
                    rcWindow.left, rcWindow.top, rcWindow.right, rcWindow.bottom, (__int64) hwnd, procId, awcText );
        }
        else
        {
            wstring match( g_AppNameRegex );
            std::wregex e( match, wregex::ECMAScript | wregex::icase );
            wstring text( awcText );

            if ( std::regex_match( text, e ) || procId == g_AppId || hwnd == (HWND) g_AppId )
            {
                g_FoundMatchingWindow = true;
                HDC hdcDesktop = GetDC( 0 );
                if ( 0 != hdcDesktop )
                {
                    HDC hdcMem = CreateCompatibleDC( hdcDesktop );
                    if ( 0 != hdcMem )
                    {
                        int width = rcWindow.right - rcWindow.left;
                        int height = rcWindow.bottom - rcWindow.top;
                        int sourceX = rcWindow.left;
                        int sourceY = rcWindow.top;

                        if ( g_Verbose )
                        {
                            printf( "original window left and top: %d %d\n", rcWindow.left, rcWindow.top );
                            printf( "original window right and bottom: %d %d\n", rcWindow.right, rcWindow.bottom );
                            printf( "sourceX %d sourceY %d, copy width %d height %d\n", sourceX, sourceY, width, height );
                        }

                        RECT rectClient;
                        if ( !g_WholeWindow && GetClientRect( hwnd, & rectClient ) )
                        {
                            if ( g_Verbose )
                                printf( "client rect: %d %d %d %d\n", rectClient.left, rectClient.top, rectClient.right, rectClient.bottom );

                            width = rectClient.right;
                            height = rectClient.bottom;

                            POINT ptUL = { 0, 0 };
                            MapWindowPoints( hwnd, HWND_DESKTOP, & ptUL, 1 );

                            sourceX = ptUL.x;
                            sourceY = ptUL.y;

                            if ( g_Verbose )
                                printf( "final sourceX %d, sourceY %d, width %d, height %d\n", sourceX, sourceY, width, height );
                        }

                        HBITMAP hbMem = CreateCompatibleBitmap( hdcDesktop, width, height );
                        if ( 0 != hbMem )
                        {
                            HANDLE hbOld = SelectObject( hdcMem, hbMem );
                            BOOL bltOK = BitBlt( hdcMem, 0, 0, width, height, hdcDesktop, sourceX, sourceY, SRCCOPY );
                            SelectObject( hdcMem, hbOld );

                            if ( bltOK )
                            {
                                // transfer ownership of the bitmap with this call

                                if ( HasBitmapChanged( hbMem, hdcMem ) )
                                {
                                    static int shownSoFar = 0;
                                    high_resolution_clock::time_point tNow = high_resolution_clock::now();
                                    long long delta = duration_cast<std::chrono::milliseconds>( tNow - tPrior ).count();
                                    tPrior = tNow;

                                    if ( ( 0 != shownSoFar ) && ( 0 == ( shownSoFar % 10 ) ) )
                                    {
                                        shownSoFar = 0;
                                        printf( "\n" );
                                    }

                                    printf( "%8lld, ", delta ); // 99999 seconds: max of about 27 hours 
                                    shownSoFar++;

                                    if ( g_Verbose )
                                        printf( "gdi count %d user count %d\n", GetGuiResources( GetCurrentProcess(), 0 ),
                                                                                GetGuiResources( GetCurrentProcess(), 1 ) );
                                }

                                // HasBitmapChanged is responsible for freeing this

                                hbMem = 0;
                            }
                            else
                            {
                                // If it's 6 == ERROR_INVALID_HANDLE it's likely due a screen saver or lock screen running.
                                // The calls to get the handles are all checked so they should be valid aside from that case.

                                int err = GetLastError();
                                if ( ERROR_INVALID_HANDLE != err )
                                    printf( "unable to blt from the desktop to the memory hdc, error %d\n", err );
                            }

                            if ( hbMem )
                                DeleteObject( hbMem );
                        }
                        else
                            printf( "unable create create a compatible Bitmap\n" );

                        ret = DeleteDC( hdcMem ); // from Create(), not Get()
                        if ( 0 == ret )
                            printf( "can't delete hdcMem: %d\n", GetLastError() );
                    }
                    else
                        printf( "unable to get the compatible memory hdc, error %d\n", GetLastError() );

                    ret = ReleaseDC( 0, hdcDesktop ); // from Get(), not Create()
                    if ( 0 == ret )
                        printf( "can't ReleaseDC hdcDesktop: %d\n", GetLastError() );
                }
                else
                    printf( "unable to get the desktop hdc, error %d\n", GetLastError() );

                return false; // end the enumeration early because we found the window
            }
        }
    }

    return true;
} //EnumWindowsProc

extern "C" int wmain( int argc, WCHAR * argv[] )
{
    // Get physical coordinates in Windows API calls. Otherwise everything is scaled
    // for the DPI of each monitor. For multi-mon with different DPIs it gets complicated.
    // This one line simplifies the code considerably.

    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    for ( int a = 1; a < argc; a++ )
    {
        WCHAR const * parg = argv[ a ];

        if ( L'-' == parg[0] || L'/' == parg[0] )
        {
            WCHAR p = tolower( parg[1] );

            if ( 'w' == p )
                g_WholeWindow = true;
            else if ( 'v' == p )
                g_Verbose = true;
            else
                Usage( "invalid argument" );
        }
        else if ( 0 == g_AppName[ 0 ] )
        {
            wcscpy( g_AppName, parg );
            g_Enumerate = false;

            int r = 0;
            int len = wcslen( g_AppName );
            for ( int i = 0; i < len; i++ )
            {
                WCHAR c = g_AppName[ i ];

                if ( '*' == c )
                {
                    g_AppNameRegex[ r++ ] = '.';
                    g_AppNameRegex[ r++ ] = '*';
                }
                else if ( '?' == c )
                {
                    g_AppNameRegex[ r++ ] = '.';
                    g_AppNameRegex[ r++ ] = '?';
                }
                else
                {
                    g_AppNameRegex[ r++ ] = '[';
                    g_AppNameRegex[ r++ ] = c;
                    g_AppNameRegex[ r++ ] = ']';
                }
            }

            g_AppNameRegex[ r++ ] = '$';
            g_AppNameRegex[ r ] = 0;

            //printf( "app name: '%ws'\n", g_AppName );
            //printf( "app name regex: '%ws'\n", g_AppNameRegex );
        }
        else
            Usage( "too many arguments specified" );
    }

    if ( g_Enumerate )
        printf( "   left    top  right bottom       hwnd   pid text\n" );
    else
    {
        WCHAR * pwcEnd = 0;
        g_AppId = wcstoull( g_AppName, &pwcEnd, 0 );

        if ( g_Verbose )
            printf( "app id %#llx == %lld\n", g_AppId, g_AppId );
    }

    CWaitPrecise wait;

    tPrior = high_resolution_clock::now();

    do
    {
        EnumWindows( EnumWindowsProc, 0 );
        if ( g_Enumerate)
            break;

        bool worked = wait.WaitInMS( 20 );
        if ( !worked )
            printf( "wait failed, error %d\n", GetLastError() );
    } while ( true );

    return 0;
} //wmain

