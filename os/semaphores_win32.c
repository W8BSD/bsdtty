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

#include "semaphores.h"

int semaphore_init(semaphore_t *sem, unsigned init)
{
	*sem = CreateSemaphore(NULL, init, LONG_MAX, NULL);
	if (*sem == NULL)
		return -1;
	return 0;
}

int semaphore_post(semaphore_t *sem)
{
	if (!ReleaseSemaphore(*sem, 1, NULL))
		return -1;
	return 0;
}

int semaphore_wait(semaphore_t *sem)
{
	if (WaitForSingleObject(*sem, INFINITE) != WAIT_OBJECT_0)
		return -1;
	return 0;
}

int semaphore_destroy(semaphore_t *sem)
{
	if (CloseHandle(*sem) == 0)
		return -1;
	return 0;
}
