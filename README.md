Simple RTTY client for FreeBSD.  Makes use of (and requires) system patch at
https://reviews.freebsd.org/D16402

This does hardware keyed RTTY using a UART.  UARTs supported by the patch are
only the 8250 based ones... no USB serial port support yet.

This also doesn't yet decode RTTY.  It's mostly a test thing for the kernel
patch.

Outstanding issues:
* There's NO WAY to open a tty without asserting RTS/DTR.
* There's NO WAY to set terminal attributes without asserting RTS/DTR.
* There are at least two different 5-bit codesets... likely more since linpsk uses '=' instead of ';'
* I'm not sure that USOS is handled correctly.
* I should idle with LTRS, not a mark signal... super tricky.
