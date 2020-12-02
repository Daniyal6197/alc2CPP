/*
 * usage: ./collapws file
 *
 * reads file specified as an argument, collapsing sequences of white space
 * characters (as determined by isspace()) to a single blank character ' '
 */
#include <stdio.h>
#include <ctype.h>

#define IN 1		/* collecting non-white-space tokens */
#define OUT 0		/* not in token */

int main(int argc, char *argv[]) {
   int c, state;
   FILE *fd;

   if (argc != 2) {
      fprintf(stderr, "usage: %s file\n", argv[0]);
      return -1;
   }
   if (! (fd = fopen(argv[1], "r"))) {
      fprintf(stderr, "%s: unable to open file %s\n", argv[0], argv[1]);
      return -2;
   }
   state = OUT;
   while ((c = fgetc(fd)) != EOF) {
      if (state == IN) {
         if (isspace(c)) {
            state = OUT;
            fputc(' ', stdout);
         } else {
            fputc(c, stdout);
         }
      } else {		/* state == OUT */
         if (! isspace(c)) {
            state = IN;
            fputc(c, stdout);
         }
      }
   }
   fputc('\n', stdout);
   return 0;
}
