#include <stdio.h>

int main(int argc, char *argv[]) {
   char buf[10240];
   FILE *fd;

   if (argc != 2) {
      fprintf(stderr, "usage: %s file\n", argv[0]);
      return -1;
   }
   fd = fopen(argv[1], "w");
   if (! fd) {
      fprintf(stderr, "%s: unable to open %s at write access\n", argv[0], argv[1]);
      return -2;
   }
   while (fgets(buf, 10240, stdin) != NULL) {
      fputs(buf, stdout);
      fflush(stdout);
      fputs(buf, fd);
   }
   fclose(fd);
   return 0;
}
