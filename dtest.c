/* dtest.c
   John Jacobsen, jacobsen@npxdesigns.com, for LBNL/IceCube
   Started March, 2003
   $Id: dtest.c,v 1.1 2005-03-14 23:25:41 jacobsen Exp $

   Very simple test program, tests basic packet I/O w/ driver 
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

#define MAX_MSG_BYTES 8092

#define pprintf(...)

int is_printable(char c) { return c >= 32 && c <= 126; }
char printable(char c) { return is_printable(c) ? c : '.'; }

void showbuf(WINDOW *w, unsigned char *buf, int n) {
  int cpl=16;
  int nl = n/cpl+1;
  printw("%d bytes: \r", n); scroll(w);
  int il, ic;
  for(il=0; il<nl; il++) {
    
    for(ic=0; ic<cpl; ic++) {
      int iof = il*cpl + ic;
      if(iof < n) {
	printw("%c", printable(buf[iof])); 
      } else {
	printw(" ");
      }
    }

    printw("  ");
    for(ic=0; ic<cpl; ic++) {
      int iof = il*cpl + ic;
      if(iof < n) {
        printw("%02x ", buf[iof]);
      } else {
	break;
      }
    }
    printw("\r"); scroll(w);
  }
  printw("\r");
  scroll(w);
}


void show_fpga(int icard) {
  /* In case of hardware timeout from the driver, show the DOR FPGA for the appropriate DOR
     card */
  char cmdbuf[1024];
  snprintf(cmdbuf,1024,"cat /proc/driver/domhub/card%d/fpga",icard);
  printf("Showing FPGA registers: %s.\n",cmdbuf);
  system(cmdbuf);
}


int usage(void) {
fprintf(stderr, 
  "Usage:\n"
	  "  dtest <file>");
  fprintf(stderr, "  <file> is of the form 00a, 00A, or /dev/dhc0w0dA\n\n");
  return 0;
}


int main(int argc, char *argv[]) {
  unsigned char rdbuf[MAX_MSG_BYTES];

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

  WINDOW * w = initscr();
  scrollok(w,1); 
  cbreak();

  move(LINES-4,0);
  printw("\n\n\t%s\n\n", filename);

  int filep;

  while(1) {
    while(1) {
      printw("[o]pen, (q)uit: "); 
      char c = getch();
      printw("\r"); scroll(w);
      if(tolower(c) == 'q') { scroll(w); endwin(); return 0; }
      if(tolower(c) == 'o' || c == '\n') {
	filep = open(filename, O_RDWR);
	if(filep <= 0) {
	  printw("Can't open file %s (%d:%s)\r", filename, errno, strerror(errno));
	  scroll(w);
	} else {
	  break;
	}
      }
    }
    
    while(1) {
      printw("[r]ead, (w)rite, (c)lose, (p)ause, (q)uit: ");
      char c = getch(); printw("\r"); scroll(w);
      if(tolower(c) == 'q') { scroll(w); close(filep); endwin(); return 0; }
      if(tolower(c) == 'c') { close(filep); break; }
      if(tolower(c) == 'p') { usleep(100*1000); }
      if(tolower(c) == 'r' || c == '\n') {
	int nr = read(filep, rdbuf, MAX_MSG_BYTES);
	if(nr>0) {
	  showbuf(w, rdbuf, nr);
	} else if(errno == EAGAIN) {
	  // Do nothing
	} else {
	  printw("%d: %s\r", errno, strerror(errno)); scroll(w);
	}
      }
      if(tolower(c) == 'w') { /* Send something to DOM */
	char wrbuf[BSIZ];
	printw("Write: ");

	getnstr(wrbuf, BSIZ-1);
	int slen = strlen(wrbuf);
	if(slen > BSIZ-1) slen = BSIZ-1;
	wrbuf[slen++] = '\r';
	
	scroll(w);
	int nw = write(filep, wrbuf, slen);
	printw("Wrote %d bytes\r", nw); scroll(w);
      }
    }
  }
  close(filep);
  return 0;
}

