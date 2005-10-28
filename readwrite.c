/* readwrite.c
   John Jacobsen, jacobsen@npxdesigns.com, for LBNL/IceCube
   Started March, 2003
   $Id: readwrite.c,v 1.3 2005-10-28 20:33:49 jacobsen Exp $

   Loopback test program for the DOR/DOM - 
   Send messages to DOM and get contents back; check to make
   sure there are no errors.
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
#define _GNU_SOURCE
#include <getopt.h>
#include <sys/poll.h>

#define MAX_MSG_BYTES 8092

#define NUMMSGS_DEFAULT 100
#define MAX_WRITE_RETRIES 10000
#define MAX_READ_RETRIES  4000
#define NMSGBUF 128
#define WRITE_DELAY 100
#define READ_DELAY  1000
#define MIN_DT_BEFORE_KBCHECK 10

static unsigned char txbuf[NMSGBUF][MAX_MSG_BYTES];
static int pktlengths[NMSGBUF];
static unsigned char rxbuf[NMSGBUF][MAX_MSG_BYTES];

#define BATCHPRINT 1 /* Set to 0 for more interactive, fast display of stats */
#define BATCHCOUNT 1000 /* Set larger for less frequent display of stats */

#define HUB 1
#define DOM 2

#define pprintf(...)

int usage(void) {
  fprintf(stderr, 
	  "Usage:\n"
	  "  readwrite HUB [options] <devfile> [num_messages] "
	  "[inter_message_delay] [open_delay]\n");
  fprintf(stderr, "  HUB needed for backwards compatibility with scripts.\n");
  fprintf(stderr, "  <devfile> is in the form 00a, 00A, or /dev/dhc0w0dA\n\n");
  fprintf(stderr, 
	  "  Options: [-s] Stuffing mode (stuff as many messages as possible into TX FIFO)\n"
	  "           [-d <msec>] delay <msec> before each write\n"
	  "           [-r <msec>] delay <msec> after last write returns -1 (TX full)\n"
	  "           [-f] Test maximal flow-control (keep send buffer full at all times)\n"
	  "           [-i] Use incremental test pattern data (1111222233334444....)\n"
	  "           [-m <maxpkt>] max message length, up to MB, below.\n"
	  "           [-p <pktlen>] pktlen between 1 and MB bytes, else random message length.\n"
	  "           [-k KB] require average bandwidth >= <KB> kilobytes/sec.\n"
	  "           [-e] put DOM in echo-mode first.\n"
	  "           [-w] wait for up to 1 second while draining stale messages"
	  "           MB == /proc/driver/domhub/bufsiz\n\n");
  return 0;
}

int is_printable(char c) { return (c >= 32 && c <= 126); }

int getBufSize(char *procFile);
int getDevFile(char *filename, int len, char *arg);
void show_buffers_hex(unsigned char *rxbuf, unsigned char *txbuf, int nrx, int ntx);
void randsleep(int usec);
void show_fpga(int icard);
void init_buffers(unsigned char *txbuf, unsigned char *rxbuf, int len);
void init_tx_buf(unsigned char *txbuf, int len, int incformat);
int perd(int icount);
void showcomstat(char * f);
int set_echo_mode(int filep, int bufsiz, float waitval);
int drain_stale_messages(int filep, int bufsiz, int dowait, float waitval);

int main(int argc, char *argv[]) {
  int nread, gotreply, write_ok;
  int nbyteswritten;
  int last_written;
  long next_cnt = 1;
  int i, icnt;
  int ipkt;
  long nummsgs;
  long msgs_written=0, msgs_ok=0;
  int mode;
  unsigned long long totbytes = 0;
  double totmb;
  double kbps;
  time_t t1, t2, delt;
  int dokb            = 0;
  int kbmin;
  int msgdelay        = 0;
  int opendelay       = 0;
  int contents_errors = 0;
  int length_errors   = 0;
  int readtimeouts    = 0;
  int read_retries    = 0;
  int verbose         = 0;
  int dowait          = 0;
  long read_try_sum   = 0;
  int check_data      = 1; /* Set to zero to kludge-supress the checking of RX pkt */
  int stuff = 0; /* If 1, stuff messages into driver/TX FIFO as fast as possible */
  int icard, ipair, idom;
  char cdom;
  int fixpkt=0, pktlen;
  int mdelay=0;
  int maxpkt;
  int flowctrl  = 0;
  int rdelay    = 0;
  int incformat = 0;
  int dosetecho = 0;
  struct pollfd  pfd;
  struct timeval tstart, tlatest;
  float deltasec;

  /************* Process command arguments ******************/

  int bufsiz=getBufSize("/proc/driver/domhub/bufsiz");
  maxpkt = bufsiz;

  while(1) {
    char c = getopt(argc, argv, "hsvwifed:m:r:p:k:");
    if (c == -1) break;

    switch(c) {
    case 'p':
      fixpkt = 1;
      pktlen = atoi(optarg);
      if(pktlen < 1 || pktlen > bufsiz) exit(usage());
      fprintf(stderr,"Will use fixed messages of length %d.\n", pktlen);
      break;
    case 'w': dowait    = 1; break;
    case 'v': verbose   = 1; break;
    case 's': stuff     = 1; break;
    case 'i': incformat = 1; break;
    case 'f': flowctrl  = 1; break;
    case 'e': dosetecho = 1; break;
    case 'k': dokb = 1; kbmin = atoi(optarg); break;
    case 'm': maxpkt    = atoi(optarg); break;
    case 'd': mdelay    = atoi(optarg); break;
    case 'r': rdelay    = atoi(optarg); break;
    case 'h':
    default: exit(usage());
    }
  }

  /* Initialize random generator for delays */
  int pid = (int) getpid();
  srand(pid);

  int argcount = argc-optind;

  if(argcount < 2) exit(usage());

  if(argcount < 3 || (nummsgs = atol(argv[optind+2])) < 0) {
    nummsgs = NUMMSGS_DEFAULT;
  }

  if(nummsgs < 0) exit(usage());

  msgdelay = 0;
  opendelay = 0;
  mode = HUB;

  if(strncmp(argv[optind], "HUB", 3)) exit(usage()); /* Keep compatibility w/ scripts */


# define BSIZ 512
  char filename[BSIZ];
  if(getDevFile(filename, BSIZ, argv[optind+1])) exit(usage());

  fprintf(stderr, "Will send/recv %ld messages to device %s.\n",
	  nummsgs, filename);

  int filep = open(filename, O_RDWR);
  if(filep <= 0) {
    fprintf(stderr,"Can't open file %s ", filename);
    perror(":");
    exit(errno);
  }

  sscanf(filename,"/dev/dhc%dw%dd%c", &icard, &ipair, &cdom);
  idom = (cdom == 'A' ? 0 : 1);

  char comstat[BSIZ];
  snprintf(comstat, BSIZ, "/proc/driver/domhub/card%d/pair%d/dom%c/comstat", icard, ipair, cdom);

  if(opendelay) usleep(opendelay);

  totmb   = 0.0;
  int firstmsg = 1;

  // Try to drain old messages first, up to 1 sec.:

  float waitval = 3.0;

  if(drain_stale_messages(filep, bufsiz, dowait, waitval)) exit(-1);

  if(dosetecho) 
    if(set_echo_mode(filep, bufsiz, waitval)) exit(-1);

  pfd.events = POLLIN;
  pfd.fd     = filep;
  gettimeofday(&tstart, NULL);

  last_written = nbyteswritten = 0;
  int last_read = 0;
  if(stuff) {
    long itxpkt = 0;
    long irxpkt = 0;
    gettimeofday(&tstart, NULL); /* Reset time */
    while(1) {
      /* Write as many records to FIFO as possible */
      if(itxpkt < nummsgs) {
	while(itxpkt < nummsgs && (itxpkt - irxpkt) < NMSGBUF) {
	  if(fixpkt) {
	    pktlengths[itxpkt%NMSGBUF] = pktlen;
	  } else {
	    pktlengths[itxpkt%NMSGBUF] = 
	      1+(int)(((float) maxpkt)*rand()/(RAND_MAX+1.0));
	  }
	  if(mdelay) usleep(mdelay*1000);
	  init_tx_buf(txbuf[itxpkt%NMSGBUF], pktlengths[itxpkt%NMSGBUF], incformat);

	  pfd.events = POLLOUT;
	  if(! poll(&pfd, 1, 0)) {
	    if(rdelay) usleep(rdelay*1000); /* Wait before read as separate test */
            break;       /* Do read cycle */
	  }
	  if(!(pfd.revents & POLLOUT)) fprintf(stderr,"POLL ERROR\n");
	  nbyteswritten = write(filep,txbuf[itxpkt%NMSGBUF], pktlengths[itxpkt%NMSGBUF]);

	  if(nbyteswritten <= 0) { 
	    fprintf(stderr,"Write EAGAIN after POLLOUT!\n");
	    exit(-1);
	  }

	  if(nbyteswritten != pktlengths[itxpkt%NMSGBUF]) {
	    fprintf(stderr,"%s: Wanted to write %d bytes, but wrote %d.\n",
		    filename, pktlengths[itxpkt%NMSGBUF], nbyteswritten);
	    exit(-1);
	  }

	  msgs_written++;
	  last_written = nbyteswritten;
	  verbose && fprintf(stderr,"%s: pkt %ld idx %d; wrote %d bytes.\n", 
			     filename, itxpkt, (int) itxpkt%NMSGBUF, pktlengths[itxpkt%NMSGBUF]);
	  itxpkt++;
	}
      }
	
      /* Read a reply */
      errno = 0;
      //fprintf(stderr,"%s: Trying read...\n", filename);
      while(1) {
	pfd.events = POLLIN;
       	if(!poll(&pfd, 1, 0)) {
	  read_retries++;
	  if(read_retries > MAX_READ_RETRIES) {
	    fprintf(stderr, "%s: Timeout (> %d retries) on read.\n", filename,
		    MAX_READ_RETRIES);
	    if(msgs_ok == 0) {
	      fprintf(stderr, "%s: Didn't read any messages back.\n",filename);
	    } else {
	      fprintf(stderr, "%s: Only read %ld messages successfully.\n", filename, msgs_ok);
	    }
	    fprintf(stderr, "Contents of FPGA for card %d:\n", icard);
	    show_fpga(icard);
	    fprintf(stderr, "Contents of comstat proc file %s:\n", comstat);
	    showcomstat(comstat);
	    exit(-1);
	  }

	  break;
	}
	if(!(pfd.revents & POLLIN)) fprintf(stderr, "POLLIN ERROR\n");

	nread = read(filep, rxbuf[irxpkt%NMSGBUF], bufsiz);
	//fprintf(stderr, "%s: read(%d,%d)\n", filename, nread, errno);
	if(nread <= 0) {
	  fprintf(stderr, "%s: read error after POLLIN! nread=%d errno=%d\n",filename, 
		  nread, errno);
	  fprintf(stderr, "Contents of FPGA for card %d:\n", icard);
	  show_fpga(icard);
	  fprintf(stderr, "Contents of comstat proc file %s:\n", comstat);
	  showcomstat(comstat);
	  exit(-1);
	} else { 
	  /* Check message contents */
	  if(nread != pktlengths[irxpkt%NMSGBUF]) {
	    fprintf(stderr, "%s: Message length mismatch (TXed %ld msgs, RXed %ld).  "
		    "Wanted %d bytes, got %d.\n",
		    filename, itxpkt, irxpkt, pktlengths[irxpkt%NMSGBUF], nread);
	    show_buffers_hex(rxbuf[irxpkt%NMSGBUF], txbuf[irxpkt%NMSGBUF], nread,
			     pktlengths[irxpkt%NMSGBUF]);
	    exit(-1);
	  }
	  int mismatches = 0;
	  int mmpos      = 0;
	  for(i=0;i<nread;i++) {
	    if(rxbuf[irxpkt%NMSGBUF][i] != txbuf[irxpkt%NMSGBUF][i]) {
	      if(mismatches == 0) mmpos = i;
	      mismatches++;
	    }
	  }

	  if(mismatches > 0) {
	    fprintf(stderr, "%s: Message mismatch in %d place(s), first mismatch at "
		    "position %d (of bytes 0..%d)... ",
		    filename, mismatches, mmpos, pktlengths[irxpkt%NMSGBUF]-1);
	    fprintf(stderr, "%ld messages were ok previous to this one.\n", msgs_ok);
	    for(i=0;i<nread;i++) {
	      fprintf(stderr, "%c", 
		      rxbuf[irxpkt%NMSGBUF][i] == txbuf[irxpkt%NMSGBUF][i] ? '-' : '+');
	      if(! (i%60)) fprintf(stderr, "\n");
	    }
	    fprintf(stderr, "\n");
	    int nl=0;
	    for(i=0;i<nread;i++) {
	      if(rxbuf[irxpkt%NMSGBUF][i] != txbuf[irxpkt%NMSGBUF][i]) {
		nl++;
		fprintf(stderr, "(%d, %d)", rxbuf[irxpkt%NMSGBUF][i], txbuf[irxpkt%NMSGBUF][i]);
		if(! (nl%10)) fprintf(stderr, "\n");
	      }
	    }

	    fprintf(stderr, "\n");
	    close(filep);
	    exit(-1);
	  }

	  /* Display statistics */
	  verbose && fprintf(stderr, "%s: Read msg %ld (idx %d); %d bytes.\n", filename, msgs_ok, 
			     (int) irxpkt%NMSGBUF, nread);
	  totbytes += nread*2;
	  last_read = nread;
	  //printf("totbytes %llu\n", totbytes);
	  read_retries = 0;
	  msgs_ok++;
	  irxpkt++;
	  gettimeofday(&tlatest, NULL);
	  deltasec = (tlatest.tv_sec - tstart.tv_sec) + 1.E-6*(tlatest.tv_usec - tstart.tv_usec);
	  kbps = (((float) totbytes)/1000.) / deltasec;
	  if(deltasec > MIN_DT_BEFORE_KBCHECK && kbps < (double) kbmin) {
	    fprintf(stderr, "%s: Data rate (%2.2f kB/s) dropped below minimum (%d kB/s)!\n",
		    filename, kbps, kbmin);
	    close(filep);
            exit(-1);
          }

	  if(verbose || perd(msgs_ok) || msgs_ok >= nummsgs) {

	    //printf("totbytes %llu\n", totbytes);
	    totmb = ((float) totbytes) / (1024.*1024.);

	    fprintf(stderr,
		    "%s: %ld msgs "
		    "(last %dB, %2.2lf MB tot, %2.2lf sec, %2.2lf kB/sec, ARR=%ld)\n",
		    filename,
		    msgs_ok, last_read, totmb, deltasec,
		    kbps, 
		    read_try_sum/msgs_ok);

	    //fprintf(stderr, "%s: Total of %ld bytes transferred.\n", filename, totbytes);
	    //fprintf(stderr, "%s: Total of %2.6lf MB transferred.\n", filename, totmb);
	    //fprintf(stderr, "%s: Data rate of %2.6lf kB/sec.\n", filename, kbps);
	    //fprintf(stderr, "%s: Got %ld messages.\n", filename, nummsgs);
	  }
	  if(msgs_ok >= nummsgs) {
	    fprintf(stderr, "%s: SUCCESS.\n", filename);
	    exit(0);
	  }
	  if(flowctrl) break; /* Do one read only before stuffing write */
	}
      } 
      //fprintf(stderr, "%s: Switching back to write...\n", filename);
      usleep(1000);
    }
    return 0;
  }

  /* Non-stuffing mode here: */
  /* FIXME: Add poll here too */
  while(1) {
    ipkt = 0;
    if(fixpkt) {
      pktlengths[ipkt] = pktlen;
    } else {
      pktlengths[ipkt] = 1+(int)(((float) bufsiz)*rand()/(RAND_MAX+1.0));
      //pktlengths[ipkt] = 1+(int)(((float) 4)*rand()/(RAND_MAX+1.0));
      //pktlengths[ipkt] = 108;
      //pktlengths[ipkt] = 1;
      //printf("%s: %d byte message.\n", filename, pktlengths[ipkt]);

    }

    if(bufsiz < pktlengths[ipkt]) {
      fprintf(stderr, "Buffer overflow.\n");
      exit(-1);
    }

    init_buffers(txbuf[ipkt], rxbuf[ipkt], pktlengths[ipkt]);
  
    write_ok = 0;
    for(icnt = 0; icnt < MAX_WRITE_RETRIES; icnt++) {
      if(mdelay) usleep(mdelay*1000);
      nbyteswritten = write(filep,txbuf[ipkt], pktlengths[ipkt]);
      // usleep(100);
      if(nbyteswritten <= 0) {
	//fprintf(stderr,"%s: EAGAIN\n", filename);
	randsleep(WRITE_DELAY);
      } else {
	if(firstmsg) {
	  firstmsg = 0;
	  t1 = time(NULL);
	  gettimeofday(&tstart, NULL);
	}
	last_written = nbyteswritten;
	msgs_written++;
	write_ok = 1;
        //fprintf(stderr,"%s: wrote %d bytes.\n", filename, nbyteswritten);
	break;
      }
    }

    if(!write_ok) {
      fprintf(stderr,"%s: Write failed (timeout).\n", filename);
      exit(-1);
    }

    if(msgdelay) usleep(msgdelay);

    usleep(READ_DELAY); /* It takes at least this long to get reply */

    gotreply = 0;
    for(icnt = 0; icnt < MAX_READ_RETRIES; icnt++) {
      read_try_sum++;
      nread = read(filep, rxbuf[ipkt], bufsiz);
      if(nread == -1){ 
	if(errno == EAGAIN) {
	  //randsleep(READ_DELAY);
	  randsleep(READ_DELAY);
	  continue;
	} else if(errno == EIO) {
	  fprintf(stderr, "%s: Hardware timeout after %ld successful messages.\n",
                  filename,  msgs_ok);
	  show_fpga(icard);
	  exit(-1);
	} else {
	  fprintf(stderr, "%s: Unknown error (%d %s) after %ld successful messages.\n",
                  filename, errno, strerror(errno), msgs_ok);
	  exit(-1);
	}
      } else if(nread < 0) {
	fprintf(stderr, "%s: Strange return value (%d) from read.\n",
		filename, nread);
	exit(-1);
      } else if(nread != nbyteswritten) {
	fprintf(stderr, "%s: Read/write mismatch after %ld successful messages: "
		"wrote %d, read %d bytes.\n",
		filename, msgs_ok,
		nbyteswritten, nread);
	show_buffers_hex(rxbuf[ipkt], txbuf[ipkt], nread, nbyteswritten);
	exit(-1);
      } else {
	gotreply = 1;
	break;
      }
    }

    if(check_data) {
      if(gotreply) {
	//      fprintf(stderr, "\nGot %d byte reply from DOM!\n", nread);
	for(i=0;i<nread;i++) {
	  if(rxbuf[ipkt][i] != txbuf[ipkt][i]) {
	    fprintf(stderr, "Message mismatch after %ld messages "
		    "on %s at position %d.\n",
		    msgs_ok,
		    filename,
		    i);
	    show_buffers_hex(rxbuf[ipkt], txbuf[ipkt], nread, nbyteswritten);
	    exit(-1);
	  }
	}
	//fprintf(stderr,"\n");
	
      } else {
	fprintf(stderr, "%s: Timeout after %ld successful messages "
		"expecting %d byte reply from DOM (%d retries).  Exiting.\n", 
		filename, msgs_ok, nbyteswritten, MAX_READ_RETRIES);
	fprintf(stderr, "\n\n");
	show_fpga(icard);
	close(filep);
	exit(-1);
      }
    }

    msgs_ok++;

    totbytes += nbyteswritten + nread;
    totmb = ((float) totbytes)/(1024.*1024.);
    t2 = time(NULL);
    gettimeofday(&tlatest, NULL);
    delt = (int) t2 - (int) t1;
    deltasec = (tlatest.tv_sec - tstart.tv_sec) + 1.E-6*(tlatest.tv_usec - tstart.tv_usec);
    //fprintf(stderr,"delt %ld deltasec %2.6lf.\n", delt, deltasec);
    kbps = (((float) totbytes)/1000.) / deltasec;
    if(deltasec > MIN_DT_BEFORE_KBCHECK && kbps < (double) kbmin) {
      fprintf(stderr, "%s: Data rate (%2.2f kB/s) dropped below minimum (%d kB/s)!\n",
	      filename, kbps, kbmin);
      close(filep);
      exit(-1);
    }

    if(verbose || perd(msgs_ok) || msgs_ok >= nummsgs) {
      next_cnt *= 2;
      fprintf(stderr,
	      "%s: %ld msgs "
	      "(last %dB, %2.2lf MB tot, %2.2lf sec, %2.2lf kB/sec, avg_rd_retries %ld)",
	      filename, 
	      msgs_ok, last_written, totmb, deltasec, 
	      kbps,
	      read_try_sum/msgs_ok);

      if(BATCHPRINT) fprintf(stderr,"\n"); 

    } else {
      //fprintf(stderr,"%d messages sent (%d bytes total).",
      //msgs_ok, totbytes);
    }
    if(msgs_written >= nummsgs) break;
  }
  fprintf(stderr,
	  "%s: %ld msgs "
	  "(last %dB, %2.2lf MB tot, %2.2lf sec, %2.2lf kB/sec, %d:%d:%d errors)  ",
	  filename, 
	  msgs_written, nbyteswritten, totmb, deltasec, 
	  kbps,
	  length_errors, contents_errors, readtimeouts);
  fprintf(stderr,"\nClosing file.\n");
  close(filep);
  fprintf(stderr, "SUCCESS\n");
  fprintf(stderr, "Done.\n");
  
  return 0;
}

int getBufSize(char * procFile) {
  int bufsiz;
  FILE *bs;
  bs = fopen(procFile, "r");
  if(bs == NULL) {
    fprintf(stderr, "Can't open bufsiz proc file.  Driver not loaded?\n");
    return -1;
  }
  fscanf(bs, "%d\n", &bufsiz);
  fclose(bs);
  return bufsiz;
}

int getDevFile(char * filename, int len, char *arg) {
  /* copy at most len characters into filename based on arg.
     If arg is of the form "00a" or "00A", file filename
     as "/dev/dhc0w0dA"; otherwise return a copy of arg. */

  int icard, ipair;
  char cdom;
  if(len < 13) return 1;
  if(arg[0] >= '0' && arg[0] <= '7') { /* 00a style */
    icard = arg[0]-'0';
    ipair = arg[1]-'0';
    cdom = arg[2];
    if(cdom == 'a') cdom = 'A';
    if(cdom == 'b') cdom = 'B';
    if(icard < 0 || icard > 7) return 1;
    if(ipair < 0 || ipair > 3) return 1;
    if(cdom != 'A' && cdom != 'B') return 1;
    snprintf(filename, len, "/dev/dhc%dw%dd%c", icard, ipair, cdom);
  } else {
    memcpy(filename, arg, strlen(arg));
  }
  return 0;
}

void show_buffers_hex(unsigned char *rxbuf, unsigned char *txbuf, int nrx, int ntx) {
  /* Show TX and RX buffers -- used when mismatch occurs */
  int nmax = nrx; if(nmax < ntx) nmax = ntx;
  int cpr  = 8;
  int nrows = (nmax+cpr-1)/cpr;
  int itx=0, irx=0;
  int irow;
  int ich;
  for(irow=0; irow<nrows; irow++) {
    fprintf(stderr, "%04d ", itx);
    for(ich=0; ich < cpr; ich++) {
      if(itx+ich < ntx) { 
	fprintf(stderr, "%02x ", txbuf[itx+ich]);
      } else {
	fprintf(stderr, "   ");
      }
    }
    itx += cpr;
    fprintf(stderr, "... ");
    for(ich=0; ich < cpr; ich++) {
      if(irx+ich < nrx) {
	fprintf(stderr, "%02x", rxbuf[irx+ich]);
      } else {
        fprintf(stderr, "  ");
      }
      if(irx+ich < nrx && irx+ich < ntx && rxbuf[irx+ich] != txbuf[irx+ich]) {
	fprintf(stderr, "*");
      } else {
	fprintf(stderr, " ");
      }
    }
    fprintf(stderr, " ... ");
    for(ich=0; ich < cpr; ich++) {
      if(irx+ich < nrx) {
	char c = rxbuf[irx+ich];
        fprintf(stderr, "%c", is_printable(c)?c:'.');
      }
    }

    irx += cpr;
    
    fprintf(stderr, "\n");
  }
}

void randsleep(int usec) {
  /* Sleep for a random amount of time up to usec microseconds */
  int j;
  j=1+(int)(((float) usec)*rand()/(RAND_MAX+1.0));
  pprintf("Delay %d, j %d.\n", usec, j);
  usleep(j);
}



void show_fpga(int icard) {
  /* In case of hardware timeout from the driver, show the DOR FPGA for the appropriate DOR
     card */
  char cmdbuf[1024];
  snprintf(cmdbuf,1024,"cat /proc/driver/domhub/card%d/fpga",icard);
  printf("Showing FPGA registers: %s.\n",cmdbuf);
  system(cmdbuf);
}

void init_buffers(unsigned char *txbuf, unsigned char *rxbuf, int len) {
  /* Do srand() first! */
  int i;
  /* Initialize send and recv buffers */
  for(i=0;i<len;i++) {
    /* txbuf[i] = (i%255); */
    txbuf[i] = (unsigned char) rand()%256;
    rxbuf[i] = 0;
  }
}

void init_tx_buf(unsigned char *txbuf, int len, int incformat) {
  /* Init TX buffer only (stuffing case) */
  /* Do srand() first! */
  int i;
  /* Initialize send buffer */
  if(incformat) {
    for(i=0; i<len; i++) txbuf[i] = (i/4)%255+1; /* Never 0 */ 
  } else {
    for(i=0;i<len;i++) txbuf[i] = (unsigned char) rand()%256;
  }
}

int perd(int icount) { /* Return true if appropriate interval for printing stats to screen */
  if(icount < 10) return 1;
  if(icount < 100 && !(icount%10)) return 1;
  if(icount < 1000 && !(icount%100)) return 1;
  if(!(icount%1000)) return 1;
  return 0;
}

void showcomstat(char * f) { /* Dump comstat proc file */
#define BIGBUF 4096
  char buf[BIGBUF];
  int fp = open(f, O_RDONLY);
  int n = read(fp, buf, BIGBUF);
  write(2, buf, n);
  close(fp);
}

int set_echo_mode(int filep, int bufsiz, float waitval) {
  char txbuf[] = "echo-mode\r";
  int  nb      = strlen(txbuf);
  int  nw = write(filep, txbuf, nb);
  if(nw != nb) {
    fprintf(stderr, "Couldn't set echo mode, write failed.\n");
    return 1;
  }
  drain_stale_messages(filep, bufsiz, 1, waitval);
  return 0;
}

void showmsg(const char * rbuf, int nread) {
  int i;
  for(i=0;i<nread;i++) {
    fprintf(stderr,"%c", is_printable(rbuf[i])?rbuf[i]:'.');
    if(!((i+1)%80)) fprintf(stderr,"\n");
  }
  fprintf(stderr,"\n");
}

int drain_stale_messages(int filep, int bufsiz, int dowait, float waitval) {
  char * rbuf = malloc(MAX_MSG_BYTES);
  if(rbuf == NULL) {
    fprintf(stderr, "Warning: malloc failed!\n");
    return 1;
  }
  int itrial=0;
  int maxtrials=1000;
  struct pollfd  pfd;
  struct timeval tstart, tlatest;
  pfd.events = POLLIN;
  pfd.fd     = filep;
  gettimeofday(&tstart, NULL);
  int draindone = 0;
  do {
    while(poll(&pfd, 1, 10)) {
      if(!(pfd.revents & POLLIN)) fprintf(stderr,"BAD POLL\n");
      int nread = read(filep, rbuf, bufsiz);
      if(nread == -1) { 
	fprintf(stderr, "Warning: POLLIN but no data!\n");
	draindone = 1;
	break;
      } else {
	fprintf(stderr,"Drained %d byte message before starting echo test...\n", nread);
	showmsg(rbuf, nread);
      }
      if(itrial > maxtrials) { 
	draindone = 1;
	break;
      }
    }
    usleep(1000);
    gettimeofday(&tlatest, NULL);
    float deltasec = (tlatest.tv_sec - tstart.tv_sec) + 1.E-6*(tlatest.tv_usec - tstart.tv_usec);
    if(deltasec > waitval) {
      draindone = 1;
      break;
    }
  } while (dowait && !draindone);
  free(rbuf);
  return 0;
}
