/* 

   readgps.c

   jacobsen@npxdesigns.com

   Started 11/13/04

   Reads/parses GPS / DOR latched time pairs (for testing)

*/

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define TSBUFLEN  22
#define MAXPROC   80
#define MAXCARD    7
#define SOH        1
#define COL      ':'
#define QUALPOS   13
#define MAXRETRIES 3

int usage(void) {
  fprintf(stderr,
	  "Usage: readgps <card_proc_file>\n"
	  "       readgps <card>\n"
	  "Options:  -d       Show difference in DOR clock ticks\n"
	  "          -o       One-shot (single readout)\n"
	  "          -w <n>   Wait n seconds between readout cycles\n"
	  "E.g., readgps /proc/driver/domhub/card0/syncgps\n");
  return -1;
}


static int die=0;
void argghhhh() { die=1; }

int isdigit_all(char *s, int max) {
  int i;
  int gotdigit = 0;
  for(i=0; i<max; i++) {
    if(s[i] == '\0') break;
    if(! isdigit(s[i])) return 0;
    gotdigit = 1;
  }
  return gotdigit;
}


int getcard(char *s, int max) {
  int i        = 0;
  int card     = 0;
  int gotdigit = 0;

  while(1) {
    if(s[i] == '\0') break;
    if(isdigit(s[i])) {
      gotdigit = 1;
      while(isdigit(s[i])) {
	card *= 10;
	card += s[i]-'0';
	i++;
      }
      return card;
    }
    i++;
  }
  return gotdigit ? card : -1;
}

int main(int argc, char ** argv) {
  int dodiff   = 0;
  int nretries = 0;
  int oneshot  = 0;
  int icard    = 9;
  int waitval  = 1;

  while(1) {
    char c = getopt(argc, argv, "hdow:");
    if (c == -1) break;
    switch(c) {
    case 'd': dodiff = 1; break;
    case 'o': oneshot = 1; break;
    case 'w': waitval = atoi(optarg); break;
    case 'h':
    default:
      exit(usage());
    }
  }

  if(argc == optind) exit(usage());

  char pfnam[MAXPROC];
  if(isdigit_all(argv[optind], MAXPROC)) {
    sprintf(pfnam, "/proc/driver/domhub/card%d/syncgps", atoi(argv[optind]));
    icard = atoi(argv[optind]);
  } else {
    /* We have a fully-qualified name */
    strncpy(pfnam, argv[optind], MAXPROC);
    icard = getcard(pfnam, MAXPROC);
  }

  if(icard < 0 || icard > MAXCARD) {
    fprintf(stderr, "Bad card value in proc file '%s'.\n", pfnam);
    exit(-1);
  }

  char tsbuf[TSBUFLEN];
  int nr, fd;
  int nok = 0;
  unsigned long long t, tlast;


  signal(SIGQUIT, argghhhh); /* "Die, suckah..." */
  signal(SIGKILL, argghhhh);
  signal(SIGINT,  argghhhh);

  while(1) {
    if(die) break;
    fd = open(pfnam, O_RDONLY);
    if(fd == -1) { 
      fprintf(stderr,"Can't open file %s: %s\n", argv[optind], strerror(errno));
      fprintf(stderr,"You may need a new driver revision: try V02-02-11 or higher.\n");
      exit(errno);
    }
    nr = read(fd, tsbuf, TSBUFLEN);
    close(fd);
    if(nr == 0) {
      sleep(waitval);
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
    fprintf(stdout,"GPS ");
    for(i=1;i<QUALPOS;i++) fprintf(stdout,"%c",tsbuf[i]);
    fprintf(stdout," TQUAL");
    switch(tsbuf[QUALPOS]) {
    case ' ': fprintf(stdout,"(' ' exclnt.,<1us)"); break;
    case '.': fprintf(stdout,"('.' v.good,<10us)"); break;
    case '*': fprintf(stdout,"('*' good,<100us)"); break;
    case '#': fprintf(stdout,"('#' fair,<1ms)"); break;
    case '?': fprintf(stdout,"('?' poor,>1ms)"); break;
    default:  fprintf(stdout," UNKNOWN!"); break;
    }
    fprintf(stdout," DOR(%d) ", icard);
    t = 0L;
    for(i=QUALPOS+1;i<TSBUFLEN;i++) {
      fprintf(stdout,"%02x", (unsigned char) tsbuf[i]);
      t <<= 8;
      t |= (unsigned char) tsbuf[i];
    }
    if(dodiff && nok > 0) {
      unsigned long dt = (unsigned long) (t - tlast);
      fprintf(stdout," dt=%lu ticks", dt);
    }
    fprintf(stdout,"\n");
    tlast = t;
    nok++;
    if(oneshot || die) break;
  }
  return 0;
} 
 
