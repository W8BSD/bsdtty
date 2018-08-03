Simple RTTY client for FreeBSD.  Makes use of (and requires) system patch at
https://reviews.freebsd.org/D16402

This does hardware keyed RTTY using a UART.  UARTs supported by the patch are
only the 8250 based ones... no USB serial port support yet.

It also decodes RTTY.

There's also an ASCII "crossed bananas" tuning aid.

This currently expects that you add the following line to your /etc/rc.local
file after installing the patched OS: `/bin/stty -f /dev/ttyu9 -rtsctr`

Controls:
TAB       Toggles TX
`         Toggles Reverse RX
[         Previous character set
]         Next character set
Backspace Configuration editor (ENTER to save, CTRL-C to abort)
CTRL-C    Exit

Outstanding issues:
* There are at least two different 5-bit codesets... likely more since linpsk uses '=' instead of ';'
* I'm not sure that USOS is handled correctly.
* I should idle with LTRS, not a mark signal... super tricky.
* You should be able to switch the shift by moving the mouse over it.
* Perhaps support autodetecting reverse mode?
* Really needs a manpage.
* You shouldn't need to exit and restart the program to load setting changes.
* A Vu meter for setting RF gain would be nice.
