Simple RTTY client for FreeBSD.  Makes use of system patch at
https://reviews.freebsd.org/D16402 for exact baud rate.  Without this patch,
there's an RTS pulse when the tty is opened, and the baudrate is 45 instead
of 1000/22 (45.454545...).  Even with the patch, you need to add the
following line to your /etc/rc.local file after installing the patched OS:
`/bin/stty -f /dev/ttyu9 -rtsctr` if you are using RTS to control PTT.

You can also control PTT using [Outrigger](https://github.com/openham/outrigger).

This does hardware keyed RTTY using a UART or AFSK.  UARTs supported by the
patch are only the 8250 based ones... no USB serial port support yet.

For hardware keyed FSK, diddles during idle are NOT sent.  When using AFSK
however, they are.

It also decodes RTTY.

There's also an ASCII "crossed bananas" tuning aid.


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

Clicking in the RX window will save the clicked callsign for use in macros.
It is also displayed in the status bar.

Right-clicking in the RX window will toggle the FIGS shift.  While
right-clicking again will reverse it again, the extents are whitespace and
shift changes, as a result, right-clicking in a "word" which contains both
FIGS and LTRS is irreversable.

Sending a macro which starts with "CQ CQ" will clear the RX window.

Outstanding issues:
* I should idle with LTRS, not a mark signal... super tricky.
* Really needs a manpage.
* Add CQRLOG integration.
* Per-mode offset to frequency so what's in bsdtty.log is directly usable.
