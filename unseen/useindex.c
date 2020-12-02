#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "timestamp.h"
#include "rtab.h"

typedef struct ndx {
    tstamp_t timeStamp;
    off_t byteOffset;
} Ndx;

int main(int argc, char *argv[]) {
    Ndx *n;
    char buf[1024];
    char indexf[256];
    char dataf[256];
    int fd;
    struct stat sb;
    FILE *fs;
    long long num;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <table>\n", argv[0]);
	return 1;
    }
    sprintf(indexf, "%s.index", argv[1]);
    sprintf(dataf, "%s.data", argv[1]);
    if ((fd = open(indexf, O_RDONLY)) == -1) {
        perror("opening index file");
	return 1;
    }
    if (fstat(fd, &sb) == -1) {
        perror("fstating index file");
	return 1;
    }
    if (!S_ISREG(sb.st_mode)) {
        fprintf(stderr, "%s is not a regular file\n", indexf);
	return 1;
    }
    if ((fs = fopen(dataf, "r")) == NULL) {
        fprintf(stderr, "Unable to open %s\n", dataf);
	return 1;
    }
    if ((n = (Ndx *)mmap(0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        perror("mmap");
	return 1;
    }
    if (close(fd) == -1) {
        perror("close");
	return 1;
    }
    //fprintf(stderr, "bytes = %lld, structs = %lld\n", sb.st_size, sb.st_size / sizeof(Ndx));
    for (num = (long long) (sb.st_size / sizeof(Ndx)) - 1; num >= 0; num--) {
        fseeko(fs, n[num].byteOffset, SEEK_SET);
	fgets(buf, sizeof(buf), fs);
	fputs(buf, stdout);
    }
    fclose(fs);
    if (munmap(n, sb.st_size) == -1) {
        perror("munmap");
	return 1;
    }
    return 0;
}    
