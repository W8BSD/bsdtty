/*-
 * Copyright (c) 2018 Stephen Hurd, W8BSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "bsdtty.h"

struct charset {
	const char *chars;
	const char *name;
};

static struct charset charsets[] = {
	{
		// From http://baudot.net/docs/smith--teletype-codes.pdf
		.name = "ITA2",
		.chars = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- '87"
		  "\r#4\x07" ",@:("
		  "5+)2$601"
		  "9?*\x0e" "./=\x0f"
	},
	{
		// From http://baudot.net/docs/smith--teletype-codes.pdf
		.name = "USTTY",
		.chars = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- \x07" "87"
		  "\r$4',!:("
		  "5\")2#601"
		  "9?&\x0e" "./;\x0f"
	},
	{
		// From ITU-T S.1 (official standard)
		/*
		 * WRU signal (who are you?) FIGS D is to operate answerback...
		 * It is therefore encoded as ENQ. (4.1)
		 * 
		 * FIGS F, G, and H are explicitly NOT DEFINED. 
		 * "arbitrary sign" such as a square to indicate an
		 * abnormal impression should occur. (4.2)
		 * 
		 * See U.11, U.20, U.22 and S.4 for NUL uses
		 */
		.name = "ITA2(S)",
		.chars = "\x00" "E\nA SIU"
		  "\rDRJNFCK"
		  "TZLWHYPQ"
		  "OBG\x0e" "MXV\x0f"
		  "\x00" "3\n- '87"
		  "\r\x05" "4\x07" ",\x00" ":("
		  "5+)2\x00" "601"
		  "9?\x00" "\x0e" "./=\x0f"
	  },
};

int charset_count = sizeof(charsets) / sizeof(charsets[0]);

char
asc2baudot(int asc, bool figs)
{
	char *ch = NULL;

	asc = toupper(asc);
	if (figs)
		ch = memchr(charsets[settings.charset].chars + 0x20, asc, 0x20);
	if (ch == NULL)
		ch = memchr(charsets[settings.charset].chars, asc, 0x40);
	if (ch == NULL)
		return 0;
	return ch - charsets[settings.charset].chars;
}

char
baudot2asc(int baudot, bool figs)
{
	if (baudot < 0 || baudot > 0x1f)
		return 0;
	return charsets[settings.charset].chars[baudot + figs * 0x20];
}

const char *
charset_name(int cs)
{
	return charsets[cs].name;
}
