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

#include <api.h>

#include <iniparser.h>

int main(int argc, char **argv)
{
	dictionary	*d;
	uint64_t	rx,tx;
	struct rig	*rig;

	d = iniparser_load("test.ini");
	if (d==NULL) {
		fprintf(stderr, "Unable to load test.ini!\n");
		return 1;
	}
	rig = init_rig(d, "main");
	if (rig == NULL) {
		fprintf(stderr, "init_rig() failed!\n");
		return 1;
	}
	printf("Set VFO: %d\n", set_vfo(rig, VFO_A));
	printf("Get VFO: %d\n", get_vfo(rig));
	printf("Set Freq: %d\n", set_frequency(rig, VFO_UNKNOWN, 28400000));
	printf("Get Freq: %"PRIu64"\n", get_frequency(rig, VFO_UNKNOWN));
	printf("Set Mode: %d\n", set_mode(rig, MODE_LSB));
	printf("Get Mode: %d\n", get_mode(rig));
	printf("Set PTT: %d\n", set_ptt(rig, true));
	printf("Get PTT: %d\n", get_ptt(rig));
	printf("Set PTT: %d\n", set_ptt(rig, false));
	printf("Set Split Freqs: %d\n", set_split_frequency(rig, 28500000, 28300000));
	printf("Get Split Freqs: %d\n", get_split_frequency(rig, &rx, &tx));
	printf("RX: %"PRIu64" TX:%"PRIu64"\n", rx, tx);
	printf("Get Freq: %"PRIu64"\n", get_frequency(rig, VFO_UNKNOWN));
	printf("Set PTT: %d\n", set_ptt(rig, true));
	printf("Get Freq: %"PRIu64"\n", get_frequency(rig, VFO_UNKNOWN));
	printf("Set PTT: %d\n", set_ptt(rig, false));
	printf("Close: %d\n", close_rig(rig));
	return 0;
}
