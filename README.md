# ud
Update Delay. Windows command-line tool that shows in milliseconds how long it's been since a window last updated.

This is useful for things like measuring runtime of an app when there is no reasonable mechanism to do so. For
example, when an emulated app is running in an emulator window (Apple 1, CP/M, etc.).

Build in a MSVC VCVars32 build window like this:

    cl /nologo ud.cxx /I.\ /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /DUNICODE /D_AMD64_ /link ntdll.lib /OPT:REF
    
usage: ud [-v] [-w] [appname]

    Update Delay: shows the milliseconds since the prior update to a window
  
    arguments: [appname]       Name of the app to capture. Can contain wildcards.
               [-v]            Verbose logging of debugging information.
               [-w]            Capture the whole window, not just the client area.
             
    sample usage: (arguments can use - or /)
  
      ud inbox*outlook
      ud *excel
      ud -w calc*
      ud 0x6bf0
      ud                       Shows titles and positions of visible top-level windows
    
    notes:
  
      - if no arguments are specified then all window positions are printed
      - appname can contain ? and * characters as wildcards
      - appname is case insensitive
      - appname can alternatively be a window handle or process id in decimal or hex
      - only visible, top-level windows are considered
      - the screen is grabbed, so obscurring windows are considered part of the app itself
      - extra pixels surrounding windows exist because Windows draws narrow inner borders
        and transparent outer edges for borders that are in fact wide
      - 'modern' app client area includes the chrome
      - doesn't work when a screen saver or lock screen appears
      - Only accurate within perhaps 1/5 of a second; it's not precise.
      
Sample output:

      96,       48,       50,       45,       54,       35,      316,      984,
     900,       33,       35,       47,       32,       33,       33,       33,
      32,       33,       33,       32,       50,       33,       34,       32,
      32,       33,       22,       44,       33,       35,       29,       35,
     300,       25,       32,       30,       31,       46,       32,       34,
     766,       25,       32,       31,       31,       30,       32,       30,
     786,       19,       32,       31,       32,       30,       36,       33,
     767,       32,       32,       29,       36,       24,       31,       32,
     779,       26,       39,       33,       21,       44,       33,       32,
     783,      984,     1016,      999,
