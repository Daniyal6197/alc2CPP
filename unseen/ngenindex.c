#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include "timestamp.h"
#include "rtab.h"

typedef struct ndx {
    tstamp_t timeStamp;
    off_t byteOffset;
} Ndx;

int main(int argc, char *argv[]) {
    Ndx n;
    char buf[1024];
    char tmp[1024];
    int size;

    fprintf(stderr, "sizeof(Ndx) = %d\n", sizeof(Ndx));
    fprintf(stderr, "sizeof(off_t) = %d\n", sizeof(off_t));
    while (! feof(stdin)) {
        n.byteOffset = ftello(stdin);
	(void) fgets(buf, sizeof(buf), stdin);
	(void) rtab_fetch_str(buf, tmp, &size);
	n.timeStamp = string_to_timestamp(tmp);
        fwrite(&n, sizeof(Ndx), 1, stdout);
    }
    fflush(stdout);
    return 0;
}    
