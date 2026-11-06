/* Adds a file to a ProDOS image with pimg_add_file (the helper installer's
 * writer). Usage: test_prodos_write <image> <folder> <name> <type> <aux> <file>
 * Check the result with: cadius CHECKVOLUME <image> */
#include "../prodos_img.h"
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	PImg		img;
	const char	*err;
	unsigned	dir, len;
	uint8_t		*data;
	FILE		*f;

	if(argc < 7) {
		fprintf(stderr, "usage: %s image folder name type aux file\n", argv[0]);
		return 2;
	}
	if(pimg_open_rw(&img, argv[1], &err) != 0) {
		printf("open: %s\n", err);
		return 1;
	}
	dir = pimg_find_dir(&img, argv[2]);
	if(!dir) {
		printf("no folder %s\n", argv[2]);
		return 1;
	}
	f = fopen(argv[6], "rb");
	fseek(f, 0, SEEK_END);
	len = (unsigned)ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc(len);
	fread(data, 1, len, f);
	fclose(f);
	if(pimg_add_file(&img, dir, argv[3], (int)strtol(argv[4], 0, 0),
			(unsigned)strtol(argv[5], 0, 0), data, len, &err) != 0) {
		printf("add: %s\n", err);
		return 1;
	}
	pimg_close(&img);
	printf("added %s (%u bytes)\n", argv[3], len);
	return 0;
}
