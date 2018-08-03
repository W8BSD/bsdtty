Simple RTTY client for FreeBSD.  Makes use of system patch at
https://reviews.freebsd.org/D16402 for exact baud rate.  Without this patch,
there's an RTS pulse when the tty is opened, and the baudrate is 45 instead
of 1000/22 (45.454545...).

This does hardware keyed RTTY using a UART.  UARTs supported by the patch are
only the 8250 based ones... no USB serial port support yet.

It also decodes RTTY.

There's also an ASCII "crossed bananas" tuning aid.

This currently expects that you add the following line to your /etc/rc.local
file after installing the patched OS: `/bin/stty -f /dev/ttyu9 -rtsctr`

Controls:

| Keystroke | Action                                                |
| --------- | ----------------------------------------------------- |
| TAB       | Toggles TX                                            |
| `         | Toggles Reverse RX                                    |
| [         | Previous character set                                |
| ]         | Next character set                                    |
| Backspace | Configuration editor (ENTER to save, CTRL-C to abort) |
| \         | Rescales the tuning aid display                       |
| CTRL-C    | Exit                                                  |
| CTRL-L    | Clear RX window                                       |

Clicking in the RX window will toggle the FIGS shift.  While clicking again will
reverse it again, the extents are whitespace and shift changes, as a result,
clicking in a "word" which contains both FIGS and LTRS is irreversable.

Sending a macro which starts with "CQ CQ" will clear the RX window.

Outstanding issues:
* I should idle with LTRS, not a mark signal... super tricky.
* Perhaps support autodetecting reverse mode?
* Really needs a manpage.
* You shouldn't need to exit and restart the program to load setting changes.
* Allow console resizing.
