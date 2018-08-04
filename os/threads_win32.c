/* Copyright (c) 2014 OpenHam
 * Developers:
 * Stephen Hurd (K6BSD/VE5BSD) <shurd@FreeBSD.org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice, developer list, and this permission notice shall
 * be included in all copies or substantial portions of the Software. If you meet
 * us some day, and you think this stuff is worth it, you can buy us a beer in
 * return
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include "threads.h"

struct win32_thread_args {
	void	(*func)(void *);
	void	*arg;
};

unsigned __stdcall win32_thread_wrapper(void *args)
{
	struct win32_thread_args	win32_args = *(struct win32_thread_args *)args;

	free(args);
	win32_args.func(win32_args.arg);
	return 0;
}

int create_thread(void(*func)(void *), void *args, thread_t *thread)
{
	struct win32_thread_args	*win32_args = malloc(sizeof(struct win32_thread_args));

	win32_args->func = func;
	win32_args->arg = args;
	*thread = (HANDLE)_beginthreadex(NULL, 0, win32_thread_wrapper, win32_args, 0, NULL);
	if (*thread == 0)
		return -1;
	return 0;
}

int wait_thread(thread_t thread)
{
	if (WaitForSingleObject(thread, INFINITE) != 0)
		return -1;
	if (CloseHandle(thread) == 0)
		return -1;
	return 0;
}
