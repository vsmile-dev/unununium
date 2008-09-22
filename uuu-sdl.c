// Copyright 2008  Segher Boessenkool  <segher@kernel.crashing.org>
// Licensed under the terms of the GNU GPL, version 2
// http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

#include <stdio.h>
#include <string.h>
#include <time.h>

// We need to include this here, since it redefines main().
#include <SDL.h>

#include "types.h"
#include "platform.h"
#include "emu.h"


#ifdef __APPLE__
void CGSSetConnectionProperty(int, int, int, int);
int _CGSDefaultConnection();
int CGSCreateCString(char *);
int CGSCreateBoolean(unsigned char);
void CGSReleaseObj(int);
#endif


int main(int argc, char *argv[])
{
	FILE *in;
	u32 i, n;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <rom-file>\n", argv[0]);
		exit(1);
	}

	in = fopen(argv[1], "rb");
	if (!in) {
		perror("Cannot read ROM file");
		exit(1);
	}

	n = fread(all_the_mem, 2, 4*N_MEM, in);

	fclose(in);

// gross, but whatever.  one day i'll fix this, but not today
#ifdef _BIG_ENDIAN
	for (i = 0; i < n; i++)
		all_the_mem[i] = (all_the_mem[i] << 8) | (all_the_mem[i] >> 8);
#endif

	platform_init();

#ifdef __APPLE__
	// Hack to speed up display refresh
	int propertyString = CGSCreateCString("DisableDeferredUpdates");
	int cid = _CGSDefaultConnection();
	CGSSetConnectionProperty(cid, cid, propertyString, CGSCreateBoolean(1));
	CGSReleaseObj(propertyString);
#endif

	srandom(time(0));

	emu();

	return 0;
}