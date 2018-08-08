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

| Control     | Action                                                 |
| ----------- | ------------------------------------------------------ |
| TAB         | Toggles TX                                             |
| `           | Toggles Reverse RX                                     |
| [           | Previous character set                                 |
| ]           | Next character set                                     |
| Backspace   | Configuration editor (ENTER to save, CTRL-C to abort)  |
| \           | Rescales the tuning aid display                        |
| CTRL-C      | Exit                                                   |
| CTRL-L      | Clear RX window                                        |
| CTRL-W      | Toggle between crossed bananas and ASCIIfall           |
| Left-arrow  | Lower squelch level by one                             |
| Right-arrow | Raise squelch level by one                             |
| Left-click  | Select callsing from RX window                         |
| Right-click | Toggle LTRS/FIGS shift on the clicked word (see below) | 

Right-clicking in the RX window will toggle the FIGS shift.  While
right-clicking again will reverse it again, the extents are whitespace and
shift changes, as a result, right-clicking in a "word" which contains both
FIGS and LTRS is irreversable.  Also, it won't shift in a beep character,
leaving it as the letter.

Sending a macro which starts with "CQ CQ" or ends with " CQ" will clear the
RX window.

Special characters can be used in macros:

| Character | Meaning         |
| --------- | --------------- |
| \         | Your callsign   |
| `         | Their callsign  |
| [         | Carriage return |
| ~         | Tab (toggle TX) |
| _         | Trailing space  |

The trailing space is only valid at the end of the macro.

Outstanding issues:
* I should idle with LTRS, not a mark signal... super tricky.
* Really needs a manpage.
* Add CQRLOG integration.
* Deal with explicit LFs in macros... CR is expanded to CRLF
