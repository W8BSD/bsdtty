Simple RTTY client for FreeBSD.  Makes use of (and requires) system patch at
https://reviews.freebsd.org/D16402

This does hardware keyed RTTY using a UART.  UARTs supported by the patch are
only the 8250 based ones... no USB serial port support yet.

This also doesn't yet decode RTTY.  It's mostly a test thing for the kernel
patch.

This currently expects that you add the following line to your /etc/rc.local
file after installing the patched OS: `/bin/stty -f /dev/ttyu9 -rtsctr`

Outstanding issues:
* There are at least two different 5-bit codesets... likely more since linpsk uses '=' instead of ';'
* I'm not sure that USOS is handled correctly.
* I should idle with LTRS, not a mark signal... super tricky.
* There are literally no options... USOS, baudrate, etc need to exist perhaps.
