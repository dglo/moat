/* 

   readgps.c

   jacobsen@npxdesigns.com

   Started 11/13/04

   Reads/parses GPS / DOR latched time pairs (for testing)

*/

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define TSBUFLEN 22
#define SOH 1
#define COL ':'
#define QUALPOS 13
#define MAXRETRIES 3

int usage(void) {
  fprintf(stderr,
	  "Usage: readgps <card_proc_file>\n"
	  "Options:  -d       Show difference in DOR clock ticks\n"
	  "          -o       One-shot (single readout)\n"
	  "E.g., readgps /proc/driver/domhub/card0/syncgps\n");
  return -1;
}


static int die=0;
void argghhhh() { die=1; }

int main(int argc, char ** argv) {
  int dodiff   = 0;
  int nretries = 0;
  int oneshot  = 0;

  while(1) {
    char c = getopt(argc, argv, "hdo");
    if (c == -1) break;
    switch(c) {
    case 'd': dodiff = 1; break;
    case 'o': oneshot = 1; break;
    case 'h':
    default:
      exit(usage());
    }
  }

  if(argc == optind) exit(usage());

  char tsbuf[TSBUFLEN];
  int nr, fd;
  int nok = 0;
  unsigned long long t, tlast;


  signal(SIGQUIT, argghhhh); /* "Die, suckah..." */
  signal(SIGKILL, argghhhh);
  signal(SIGINT,  argghhhh);

  while(1) {
    if(die) break;
    fd = open(argv[optind], O_RDONLY);
    if(fd == -1) { 
      fprintf(stderr,"Can't open file %s: %s\n", argv[optind], strerror(errno));
      fprintf(stderr,"You may need a new driver revision: try V02-02-11 or higher.\n");
      exit(errno);
    }
    nr = read(fd, tsbuf, TSBUFLEN);
    close(fd);
    if(nr == 0) {
      sleep(1);
      if(nretries++ > MAXRETRIES) {
	fprintf(stderr,"ERROR - No GPS data available, check hardware/firmware setup.\n");
	exit(-1);
      }
      continue;
    }
    nretries = 0;
    if(nr != TSBUFLEN) {
      fprintf(stderr,"Didn't read enough bytes from %s.  Wanted %d, got %d.\n",
	      argv[optind], TSBUFLEN, nr);
      exit(-1);
    }
    int i;
    if(tsbuf[0] != SOH || tsbuf[4] != COL || tsbuf[7] != COL || tsbuf[10] != COL) {
      fprintf(stderr,"Bad time string/timestamp format; got:\n");
      for(i=0;i<TSBUFLEN;i++) {
	fprintf(stderr,"Position %d byte 0x%02x\n", i, tsbuf[i]);
      }
    }
    fprintf(stderr,"GPS ");
    for(i=1;i<QUALPOS;i++) fprintf(stderr,"%c",tsbuf[i]);
    fprintf(stderr," TQUAL");
    switch(tsbuf[QUALPOS]) {
    case ' ': fprintf(stderr,"(' ' exclnt.,<1us)"); break;
    case '.': fprintf(stderr,"('.' v.good,<10us)"); break;
    case '*': fprintf(stderr,"('*' good,<100us)"); break;
    case '#': fprintf(stderr,"('#' fair,<1ms)"); break;
    case '?': fprintf(stderr,"('?' poor,>1ms)"); break;
    default:  fprintf(stderr," UNKNOWN!"); break;
    }
    fprintf(stderr," DOR ");
    t = 0L;
    for(i=QUALPOS+1;i<TSBUFLEN;i++) {
      fprintf(stderr,"%02x", (unsigned char) tsbuf[i]);
      t <<= 8;
      t |= (unsigned char) tsbuf[i];
    }
    if(dodiff && nok > 0) {
      unsigned long dt = (unsigned long) (t - tlast);
      fprintf(stderr," dt=%lu ticks", dt);
    }
    fprintf(stderr,"\n");
    tlast = t;
    nok++;
    if(oneshot || die) break;
  }
  return 0;
} 
 
