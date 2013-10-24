/* tcaltest.c - John Jacobsen, john@johnj.com, for LBNL/IceCube, Jul. 2003 
   Tests functionality of time calibration
   $Id: tcaltest.c,v 1.7 2005-11-30 00:26:44 jacobsen Exp $
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#define _GNU_SOURCE
#include <getopt.h>
#include <signal.h>

#include <linux/types.h>
#include "dh_tcalib.h"

#define DOM_WF_THRESH 50
#define DOR_WF_THRESH 50
#define MAX_DOM_TSTAMP_DIFF 1000
#define MAX_DOR_TSTAMP_DIFF 1600

//#define DEBUG
#ifdef DEBUG
# define pprintf printf
#else
# define pprintf(...) /* Nothing */
#endif

int usage(void) { 
  printf("Usage:  tcaltest [<card><pair><dom>|<procfile>] <ntrials>\n"
	 "\t[-t <tcal_delay_usec>]\n"
	 "\t[-s <skip_bytes>]\n"
	 "\t[-f <data_file>]\n"
	 "\t[-q : continue when data quality check fails]\n"
	 "\t[-d <dor_clock_mhz> (default 10)\n"
	 "\t\tIMPORTANT: use -d 20 for non-DSB configurations\n");
  return -1;
}

struct dh_tcalib_t tcalrec;

int tcal_data_ok(int dor_clock, struct dh_tcalib_t *tcalrec, int itrial, u64 last_tx, u64 last_rx);
void show_tcalrec(FILE *fp, struct dh_tcalib_t *tcalrec);
int getProcFile(char *filename, int len, char *arg, int * icard, int * ipair, char * cdom);
int chkpower(int icard, int ipair);

#define NS 512

void dump_fpga(int icard) {
  char fpgacmd[NS];
  printf("Dumping FPGA proc file for card %d...\n", icard);
  snprintf(fpgacmd, NS, "cat /proc/driver/domhub/card%d/fpga", icard);
  system(fpgacmd);
}

void dump_comstat(int icard, int ipair, char cdom) {
  char comstatcmd[NS];
  printf("Dumping comstat proc file for card %d pair %d DOM %c...\n", icard, ipair, cdom);
  snprintf(comstatcmd, NS, "cat /proc/driver/domhub/card%d/pair%d/dom%c/comstat",
	   icard, ipair, cdom);
  system(comstatcmd);
}

static int die=0;
void argghhhh() { fprintf(stderr,"Caught signal, bye...\n"); die=1; }  

int main(int argc, char *argv[]) {
  //unsigned char tcalbuf[NTCAL];
  char single[] = "single\n";
  int no_show = 0;
  int file, pid;
  int MAX_TCAL_TRIES = 3000;
  int nread, nwritten, ntrials = 1, itry;
  unsigned long icalib;
  unsigned long tdelay = 1000000;
  int option_index = 0, argstart, argcount;
  char datafile[NS];
  unsigned char tcalrec_packed[DH_TCAL_STRUCT_LEN];
  int offset = 0;

#define MAXSKIP 1024
  char skipbuf[MAXSKIP];
  int icard, ipair;
  char cdom;
  int dofile = 0;
  int dor_clock = 10; /* 10 MHz (DSB) version is default */
  int skipbytes = 0;
  int survive_dqfail = 0;
  char c;
  static struct option long_options[] =
    {
      {"help", 0, 0, 0},
      {"tcaldelay", 0, 0, 0}, 
      {"skip", 0, 0, 0},
      {"file", 0, 0, 0},
      {"dor-clock", 0, 0, 0},
      {0, 0, 0, 0}
    };

  /************* Process command arguments ******************/

  while(1) {
    c = getopt_long (argc, argv, "qht:f:s:d:o:",
		     long_options, &option_index);
    if (c == -1)
      break;

    switch(c) {
    case 'h':
      exit(usage());
    case 't':
      tdelay = atol(optarg);
      printf("Will use time delay of %ld microseconds between calibrations.\n",tdelay);
      break;
    case 'd':
      dor_clock = atol(optarg);
      break;
    case 'f':
      strncpy(datafile,optarg,NS);
      fprintf(stderr, "Will take data from file %s....\n",datafile);
      dofile = 1;
      break;
    case 's':
      skipbytes = atoi(optarg);
      if(skipbytes > MAXSKIP) {
	fprintf(stderr, "Can't skip more than %d bytes!\n", MAXSKIP);
	exit(-1);
      }
      fprintf(stderr, "Will skip the first %d bytes...\n", skipbytes);
      break;
    case 'q': survive_dqfail = 1; break;
    default:
      exit(usage());
    }
  }

  argstart = optind;
  argcount = argc-optind;

  if(!dofile && argcount < 1) exit(usage());

  if(argcount >= 2) ntrials = atoi(argv[optind+1]);

  if(argcount >= 3) no_show = !strncmp(argv[optind+2], "noshow", 6);

  if(ntrials == 0) exit(0);

  /* Initialize random generator for delays */
  pid = (int) getpid();
  pprintf("Pid is %d.\n", pid);
  srand(pid);

  long rdtimeouts = 0;
  long wrtimeouts = 0;
  long dqfail     = 0;
  long success    = 0;

  if(dofile) { 
    fprintf(stderr, "Opening file %s for reading...\n", datafile);
    file = open(datafile, O_RDONLY);
    if(file == -1) {
      printf("Can't open file %s: %s\n", datafile, strerror(errno));
      exit(errno);
    }
    if(skipbytes) {
      int nr;
      if((nr=read(file, skipbuf, skipbytes)) != skipbytes) {
	fprintf(stderr, "Short read (only %d of %d bytes).\n", nr, skipbytes);
	exit(-1);
      }
    }
  } else {
    if(getProcFile(datafile, NS, argv[optind], &icard, &ipair, &cdom)) exit(usage());
  }

  signal(SIGQUIT, argghhhh); /* "Die, suckah..." */
  signal(SIGKILL, argghhhh);
  signal(SIGINT,  argghhhh);

  u64 last_dor_tx, last_dor_rx;

  for(icalib=0; icalib < ntrials; icalib++) {

    if(die) break; /* Signal handler argghhhh sets die so we quit */

    /* Make sure power to this wire pair is on */
    if(chkpower(icard, ipair)) {
      fprintf(stderr, "%s: Can't perform tcalib, card %d pair %d not powered on.\n",
	      datafile, icard, ipair);
      exit(-1);
    }

    if(icalib > 0 && !(icalib % 10)) {
      fprintf(stderr, "%s: %ld tcals, %ld rdtouts, %ld wrtouts, %ld bad.\n",
	      datafile, success, rdtimeouts, wrtimeouts, dqfail);
    }

    if(!dofile) {
      file = open(datafile, O_RDWR);
      if(file <= 0) {
	fprintf(stderr,"Can't open file %s: %s\n", datafile, strerror(errno));
	exit(errno);
      }
    }

    if(!dofile) {
      for(itry=0; itry < MAX_TCAL_TRIES; itry++) {
	nwritten = write(file, single, strlen(single));
	if(nwritten != strlen(single)) {
	  if(itry == MAX_TCAL_TRIES-1) {
	    printf("cal(%ld) WRITE FAILED: TIMEOUT\n", icalib);
	    fprintf(stderr,"Time calibration write timeout in trial %ld.\n", icalib);
	    dump_fpga(icard);
	    dump_comstat(icard, ipair, cdom);
	    exit(-1);
	  } else {
	    if(! no_show) {
	      printf("cal(%ld) WRITE RETRY(%d)\n",icalib, itry);
	    }
	    usleep(2000);
	    continue;
	  }
	} else {
	  pprintf("cal(%ld) WRITE SUCCEEDED\n",icalib);
	  break;
	}
      }
    }

    usleep(10000); 

    for(itry=0; itry < MAX_TCAL_TRIES; itry++) {
        nread = read(file, tcalrec_packed, DH_TCAL_STRUCT_LEN);        
        if(nread != DH_TCAL_STRUCT_LEN) {
            if(itry == MAX_TCAL_TRIES-1) {
                printf("cal(%ld) READ FAILED: TIMEOUT!!!\n", icalib);
                fprintf(stderr,"Time calibration read timeout in trial %ld.\n", icalib);
                dump_fpga(icard);
                dump_comstat(icard, ipair, cdom);
                exit(-1);
            } else {
                if(! no_show) {
                    fprintf(stderr,"cal(%ld) READ RETRY(%d)\n", icalib, itry);
                }
                usleep(1000);
                continue;
            }         
        } else {
            offset = 0;
            /* Unpack into struct */
            memcpy(&tcalrec.hdr, tcalrec_packed+offset, sizeof(tcalrec.hdr));
            offset += sizeof(tcalrec.hdr);
            memcpy(&tcalrec.dor_t0, tcalrec_packed+offset, sizeof(tcalrec.dor_t0));
            offset += sizeof(tcalrec.dor_t0);
            memcpy(&tcalrec.dor_t3, tcalrec_packed+offset, sizeof(tcalrec.dor_t3));
            offset += sizeof(tcalrec.dor_t3);
            memcpy(tcalrec.dorwf, tcalrec_packed+offset, sizeof(tcalrec.dorwf[0])*DH_MAX_TCAL_WF_LEN);
            offset += sizeof(tcalrec.dorwf[0])*DH_MAX_TCAL_WF_LEN;
            memcpy(&tcalrec.dom_t1, tcalrec_packed+offset, sizeof(tcalrec.dom_t1));
            offset += sizeof(tcalrec.dom_t1);
            memcpy(&tcalrec.dom_t2, tcalrec_packed+offset, sizeof(tcalrec.dom_t2));
            offset += sizeof(tcalrec.dom_t2);
            memcpy(tcalrec.domwf, tcalrec_packed+offset, sizeof(tcalrec.domwf[0])*DH_MAX_TCAL_WF_LEN);
            offset += sizeof(tcalrec.domwf[0])*DH_MAX_TCAL_WF_LEN;
            
            if(! tcal_data_ok(dor_clock, &tcalrec, icalib, last_dor_tx, last_dor_rx)) {
                fprintf(stderr,"Time calibration data failed quality check in trial %ld.\n",icalib);
                if(survive_dqfail) {
                    dqfail++;
                } else 
                    exit(-1);
            } else {
                last_dor_tx = tcalrec.dor_t0;
                last_dor_rx = tcalrec.dor_t3;
            }
            if(! no_show) {
                printf("cal(%ld) ", icalib);
                show_tcalrec(stdout, &tcalrec);
                fflush(stdout);
            }
            success++;
        }
        if(! no_show) {
            printf("\n");
        }
        if(!dofile) close(file);
        break;
    }    
    usleep(tdelay);
  }

  if(dofile) close(file);

  fprintf(stderr, "Done:\n");
  fprintf(stderr, "%s: %ld tcals, %ld rdtouts, %ld wrtouts, %ld bad.\n",
	  datafile, success, rdtimeouts, wrtimeouts, dqfail);
  return 0;

}

int tcal_data_ok(int dor_clock, struct dh_tcalib_t * tcalrec, int itrial,
		 u64 last_dor_tx, u64 last_dor_rx) {
  int dom_baseline, dor_baseline;
  int iwf, foundthresh;

#define CLOCKBITS 48
#define MASK      ((1LL << CLOCKBITS)-1)
#define DOR_FREQ  20000000

  if(itrial > 0 && last_dor_tx > tcalrec->dor_t0 
     && tcalrec->dor_t0 > 10*DOR_FREQ) { // Kludgy, but allow rollover if TXed value is
                                         // within the first 10 seconds of rollover
    fprintf(stderr, "Bad DOR TX timestamp order (cur=%lld, last=%lld)\n",
	    (unsigned long long) tcalrec->dor_t0, (unsigned long long) last_dor_tx);
    show_tcalrec(stderr, tcalrec);
    return 0;
  }

  if(itrial > 0 && last_dor_rx > tcalrec->dor_t3
     && tcalrec->dor_t3 > 10*DOR_FREQ) {
    fprintf(stderr, "Bad DOR RX timestamp order (cur=%lld, last=%lld)\n",
            (unsigned long long) tcalrec->dor_t3, (unsigned long long) last_dor_rx);
    show_tcalrec(stderr, tcalrec);
    return 0;
  }

  if(((tcalrec->dor_t3 - tcalrec->dor_t0)&MASK) > MAX_DOR_TSTAMP_DIFF) {
    fprintf(stderr, "Bad DOR timestamps (wrong order or diff. to big):\n");
    show_tcalrec(stderr, tcalrec);
    return 0;
  }

  if(((tcalrec->dom_t2 - tcalrec->dom_t1)&MASK) > MAX_DOM_TSTAMP_DIFF) {
    fprintf(stderr, "Bad DOM timestamps (wrong order or diff. to big):\n");
    show_tcalrec(stderr, tcalrec);
    return 0;
  }

  /* Consistency check to timestamps: */
  float dor_dom_ratio = (float) dor_clock / 40.0;

  if((float) ((tcalrec->dor_t3 - tcalrec->dor_t0)&MASK) < 
     dor_dom_ratio * ((float) ((tcalrec->dom_t2 - tcalrec->dom_t1)&MASK))) {
    fprintf(stderr, "DOR or DOM timestamp problem (delta_dor < (%2.1f)*delta_dom:\n",
	    dor_dom_ratio);
    show_tcalrec(stderr, tcalrec);
    return 0;
  }

#if DH_MAX_TCAL_WF_LEN < 4
#error DH_MAX_TCAL_WF_LEN too small
#endif

  /* Establish DOR WF baseline */
  dor_baseline = (int) 
    ((tcalrec->dorwf[0] + tcalrec->dorwf[1] + tcalrec->dorwf[2] + tcalrec->dorwf[3])/4.0 + 0.5);

  /* Verify that some part of DOR waveform is DOR_WF_THRESH counts above baseline */
  foundthresh = 0;
  for(iwf=0; iwf < DH_MAX_TCAL_WF_LEN; iwf++) {
    if(tcalrec->dorwf[iwf] - dor_baseline > DOR_WF_THRESH) {
      foundthresh++;
      break;
    }
  }
  if(!foundthresh) {
    fprintf(stderr, "Bad DOR waveform (never exceeds threshold):\n");
    show_tcalrec(stderr, tcalrec);
    return 0;
  }

  /* Establish DOM WF baseline */
  dom_baseline = (tcalrec->domwf[0] + tcalrec->domwf[1] + tcalrec->domwf[2] + tcalrec->domwf[3])/4;

  /* Verify that some part of DOM waveform is DOM_WF_THRESH counts above baseline */
  foundthresh = 0;
  for(iwf=0; iwf < DH_MAX_TCAL_WF_LEN; iwf++) {
    if(tcalrec->domwf[iwf] - dom_baseline > DOM_WF_THRESH) {
      foundthresh++;
      break;
    }
  }
  if(!foundthresh) {
    fprintf(stderr, "Bad DOM waveform (never exceeds threshold):\n");    
    show_tcalrec(stderr, tcalrec);
    return 0;
  }


  return 1;
}

void show_tcalrec(FILE *fp, struct dh_tcalib_t * tcalrec) {
  int i;
  fprintf(fp, "dor_tx(0x%llx) ", (unsigned long long) tcalrec->dor_t0);
  fprintf(fp, "dor_rx(0x%llx) ", (unsigned long long) tcalrec->dor_t3);
  fprintf(fp, "dom_rx(0x%llx) ", (unsigned long long) tcalrec->dom_t1);
  fprintf(fp, "dom_tx(0x%llx)\n", (unsigned long long) tcalrec->dom_t2);
  fprintf(fp, "dor_wf(");
  for(i=0; i < DH_MAX_TCAL_WF_LEN-1; i++) {
    fprintf(fp, "%d, ", tcalrec->dorwf[i]);
  }
  fprintf(fp, "%d)\n", tcalrec->dorwf[DH_MAX_TCAL_WF_LEN-1]);
  
  fprintf(fp, "dom_wf(");
  for(i=0; i < DH_MAX_TCAL_WF_LEN-1; i++) {
    fprintf(fp, "%d, ", tcalrec->domwf[i]);
  }
  fprintf(fp, "%d)\n", tcalrec->domwf[DH_MAX_TCAL_WF_LEN-1]);
}

int getProcFile(char * filename, int len, char *arg, int *icard, int *ipair, char *cdom) {
  /* copy at most len characters into filename based on arg.
     If arg is of the form "00a" or "00A", file filename
     as "/proc/driver/domhub/card0/pair0/domA/tcalib"; 
     otherwise return a copy of arg. Fill in card, pair, dom */

  if(len < 3) return 1;
  if(arg[0] >= '0' && arg[0] <= '7') { /* 00a style */
    *icard = arg[0]-'0';
    *ipair = arg[1]-'0';
    *cdom = arg[2];
    if(*cdom == 'a') *cdom = 'A';
    if(*cdom == 'b') *cdom = 'B';
    if(*icard < 0 || *icard > 7) return 1;
    if(*ipair < 0 || *ipair > 3) return 1;
    if(*cdom != 'A' && *cdom != 'B') return 1;
    snprintf(filename, len, "/proc/driver/domhub/card%d/pair%d/dom%c/tcalib", 
	     *icard, *ipair, *cdom);
  } else {
    if(sscanf(arg, "/proc/driver/domhub/card%d/pair%d/dom%c/tcalib",
	      icard, ipair, cdom) != 3) {
      fprintf(stderr, "Couldn't parse proc file string %s, sorry.\n", arg);
      return 1;
    }
    memcpy(filename, arg, strlen(arg)>len?len:(strlen(arg)+1));
  }
  return 0;
}

int chkpower(int icard, int ipair) {
  char buf[1024];
  char target[1024];
  snprintf(buf,    1024, "/proc/driver/domhub/card%d/pair%d/pwr", icard, ipair);
  snprintf(target, 1024, "Card %d Pair %d power status is on.\n", icard, ipair);
  int fp = open(buf, O_RDONLY);
  if(fp == -1) {
    fprintf(stderr, "Can't open file %s: %s\n", buf, strerror(errno));
    return 1;
  }
  int nb = read(fp, buf, 1024);
  if(nb<34) {
    fprintf(stderr, "Short read of %d bytes from pwr proc file.\n", nb);
    return 1;
  }
  if(strncmp(buf, target, nb)) {
    return 1;
  }
  close(fp);
  return 0;
}
