/* singleopen.c
   John Jacobsen, jacobsen@npxdesigns.com, for LBNL/IceCube
   Started 9/27/05
   $Id: singleopen.c,v 1.1 2005-10-03 21:01:17 jacobsen Exp $
   Try to open/close DOR driver /dev file.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <ctype.h>
#define _GNU_SOURCE
#include <getopt.h>

int usage(void) {
fprintf(stderr, 
  "Usage:\n"
	  "  singleopen <file>");
  fprintf(stderr, "  <file> is of the form 00a, 00A, or /dev/dhc0w0dA\n\n");
  return 0;
}


int main(int argc, char *argv[]) {

  static struct option long_options[] =
    {
      {"help", 0, 0, 0},
      {0, 0, 0, 0}
    };

  /************* Process command arguments ******************/

  int bufsiz=0;
  FILE *bs;
  bs = fopen("/proc/driver/domhub/bufsiz","r");
  if(bs == NULL) {
    fprintf(stderr, "Can't open bufsiz proc file.  Driver not loaded?\n");
    exit(-1);
  }
  fscanf(bs, "%d\n", &bufsiz);
  fclose(bs);
  if(bufsiz == 0) {
    fprintf(stderr, "Can't get buffer size from bufsiz proc file.\n");
    exit(-1);
  }


  int option_index=0;
  while(1) {
    char c = getopt_long (argc, argv, "h",
                     long_options, &option_index);
    if (c == -1) break;

    switch(c) {
    case 'h':
    default: exit(usage());
    }
  }

  int argcount = argc-optind;

  if(argcount < 1) exit(usage());

  char * filename = argv[optind];
# define BSIZ 512
  char filbuf[BSIZ];
  int icard, ipair;
  char cdom;
  if(filename[0] >= '0' && filename[0] <= '7') { /* 00a style */
    icard = filename[0]-'0';
    ipair = filename[1]-'0';
    cdom = filename[2];
    if(cdom == 'a') cdom = 'A';
    if(cdom == 'b') cdom = 'B';
    if(icard < 0 || icard > 7) exit(usage());
    if(ipair < 0 || ipair > 3) exit(usage());
    if(cdom != 'A' && cdom != 'B') exit(usage());
    snprintf(filbuf, BSIZ, "/dev/dhc%dw%dd%c", icard, ipair, cdom);
    filename = filbuf;
  }

  int filep = open(filename, O_RDWR);
  if(filep <= 0) {
    fprintf(stderr, "Can't open file %s (%d:%s)\r", filename, errno, strerror(errno));
    exit(-1);
  }
  printf("OK.\n");
  close(filep);
  return 0;
}

