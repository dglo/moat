/* rndpkt.c - John Jacobsen, john@johnj.com, for LBNL/IceCube, Mar. 2003 
   Send small request for larger packets; make sure packets are correct.
   C.f. echo-pkt-mode in Iceboot. 
   Use echo-pkt-mode.pl to prep DOMs (send Iceboot command) first */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEND_MSG_BYTES   8
#define MAX_RECV_MSG_BYTES   4096
#define MAX_PKT_DEFAULT      100
#define NUMMSGS_DEFAULT      100
#define MAX_WRITE_RETRIES    100000
#define MAX_READ_RETRIES     1000
#define WRITE_DELAY          100
#define READ_DELAY           100

#define BATCHPRINT 1 /* Set to 0 for more interactive, fast display of stats */
#define BATCHCOUNT 1000 /* Set larger for less frequent display of stats */

static char usage[]="Usage: rndpkt <devfile> [num_messages] [max_pkt_len]\n";

#define HUB 1
#define DOM 2
#define pfprintf(...)
#define pprintf(...)

int is_printable(char c) {
  if(c >= 32 && c <= 126) return 1;
  return 0;
}

void show_buffers(char *rxbuf, char *txbuf, int n) {
  int i;
  fprintf(stderr,"RX buffer:\n");
  for(i=0; i<n; i++) {
    fprintf(stderr, "%x ", (unsigned char) rxbuf[i]);
    if(!((i+1)%80)) fprintf(stderr,"\n");
  }
  fprintf(stderr,"\n");

  fprintf(stderr,"TX buffer:\n");
  for(i=0; i<n; i++) {
    fprintf(stderr, "%x ", (unsigned char) txbuf[i]);
    if(!((i+1)%80)) fprintf(stderr,"\n");
  }
  fprintf(stderr,"\n");

}

void randsleep(int usec) {
  int j;
  j=1+(int)(((float) usec)*rand()/(RAND_MAX+1.0));
  //printf("Delay %d, j %d.\n", usec, j);
  usleep(j);
}

int main(int argc, char *argv[]) {
  unsigned char txbuf[MAX_SEND_MSG_BYTES];
  unsigned char rxbuf[MAX_RECV_MSG_BYTES];
  int file;
  int nread, gotreply, write_ok;
  int nbyteswritten;
  long next_cnt = 1;
  int i, icnt;
  int nummsgs;
  char *domfile;
  long msgs_written;
  double totmb;
  time_t t1, t2, delt;
  int firstmsg  = 1;
  int maxpkt  = MAX_PKT_DEFAULT;
  int opendelay = 0;
  int contents_errors = 0;
  int length_errors   = 0;
  int readtimeouts    = 0;
  int pid;
  int pktok;
  unsigned long ul, lastul, expectul, a=69069, c=1;
  unsigned int pktlen;
  unsigned long seed = 0;

  /* Initialize random generator for delays */
  pid = (int) getpid();
  //printf("Pid is %d.\n", pid);
  srand(pid);

  if(argc < 2) {
    fprintf(stderr,usage);
    exit(-1);
  }

  if(argc < 3 || (nummsgs = atoi(argv[2])) <= 0) {
    nummsgs = NUMMSGS_DEFAULT;
  }

  if(argc < 4 || (maxpkt = atoi(argv[3])) < 0) {
    maxpkt = MAX_PKT_DEFAULT;
  } else if(maxpkt > MAX_PKT_DEFAULT) {
    fprintf(stderr, "Sorry, can't have packet size > %d longwords (yet).\n",
	    MAX_PKT_DEFAULT);
    exit(-1);
  }

  if(argc < 5 || (opendelay = atoi(argv[4])) < 0) {
    opendelay = 0;
  }

  domfile = argv[1];
  fprintf(stderr, "Will send/recv %d messages to device %s.\n",
	  nummsgs, domfile);
   
  file = open(domfile, O_RDWR);
  if(file <= 0) {
    fprintf(stderr,"Can't open file %s ", domfile);
    perror(":");
    exit(errno);
  }
   

  if(opendelay) usleep(opendelay);
  
  totmb   = 0.0;
  msgs_written = 0;
  firstmsg = 1;
   
  // HUB mode only:

  // Try to drain old messages first:
  for(icnt = 0; icnt < 1000000; icnt++) {
    nread = read(file, rxbuf, MAX_RECV_MSG_BYTES);
    if(nread <= 0) {
      //fprintf(stderr, "No stale message left in driver.\n");
      break;
    }
    //fprintf(stderr, 
    //"Read stale message (%d bytes) from driver before starting test.\n",nread);
  }

  lastul = 0;

  while(1) {

    /* Initialize send buffer */

    pktlen   = 1+(int)(((float) maxpkt)*rand()/(RAND_MAX+1.0));

    pfprintf(stderr,"pktlen=%d.\n",pktlen);
    seed     = msgs_written;
    lastul   = seed;
    txbuf[0] = seed & 0xFF;
    txbuf[1] = (seed>>8) & 0xFF;
    txbuf[2] = (seed>>16) & 0xFF;
    txbuf[3] = (seed>>24) & 0xFF;
    //txbuf[1] = txbuf[2] = txbuf[3] = 0;
    txbuf[4] = pktlen & 0xFF;
    txbuf[5] = (pktlen>>8) & 0xFF;
    txbuf[6] = txbuf[7] = 0;


    write_ok = 0;
    for(icnt = 0; icnt < MAX_WRITE_RETRIES; icnt++) {
      nbyteswritten = write(file,txbuf, MAX_SEND_MSG_BYTES);
      // usleep(100);
      if(nbyteswritten <= 0) {
	randsleep(WRITE_DELAY);
      } else {
	if(firstmsg) {
	  firstmsg = 0;
	  t1 = time(NULL);
	}
	msgs_written++;
	write_ok = 1;
	//fprintf(stderr,"Wrote a message to the DOM.\n");
	break;
      }

    }

    if(!write_ok) {
      fprintf(stderr,"%s: Write failed (timeout).\n", domfile);
      exit(-1);
    }

    gotreply = 0;
    for(icnt = 0; icnt < MAX_READ_RETRIES; icnt++) {
      nread = read(file, rxbuf, MAX_RECV_MSG_BYTES);
      if(nread <= 0) {
	randsleep(READ_DELAY);
	continue;
      } else if(nread != pktlen*4) {
	fprintf(stderr, "Read/write mismatch: expected %d, read %d bytes.\n",
		pktlen*4, nread);
	exit(-1);
	// length_errors++;
      } else {
	gotreply = 1;
	pfprintf(stderr, "Read/write ok: wrote %d, read %d bytes.\n",
		nbyteswritten, nread);
	break;
      }
    }

    if(gotreply) {
      pktok = 1;
      for(i=0; i<pktlen; i++) {
	ul = rxbuf[i*4] + (rxbuf[i*4+1] << 8) + (rxbuf[i*4+2] << 16) + (rxbuf[i*4+3] << 24);
	expectul = a*lastul + c;
	pfprintf(stderr,"pktlen=%d expectul=%ld ul=%ld lastul=%ld a=%ld c=%ld\n",
	       pktlen, expectul, ul, lastul, a, c);
	if(ul != expectul) {
	  fprintf(stderr,"packet word %d: got %lu, wanted %lu.\n", i, ul, expectul);
	  pktok = 0;
	  break;
	}
	lastul = ul;
      }
      if(! pktok) {
	fprintf(stderr,"%ldth packet: error from %s.\n", msgs_written-1, domfile);
	exit(-1);
      }

      // fprintf(stderr,"\n");
      
    } else {
      fprintf(stderr, "%s: Timeout expecting %d byte reply from DOM.\n", 
	      domfile,nbyteswritten);
      readtimeouts++;
      fprintf(stderr, "\n\n");

      //system("cat /proc/driver/domhub/card0/fpga");
      //fprintf(stderr, "WARNING: FPGA proc file name hardcoded (card 0 may not correspond"
      //" to %s).\n",domfile);
      return 1;
    }

    //show_buffers(rxbuf, txbuf, nread);

    totmb += (nbyteswritten + nread)/(1024.*1024.);
    t2 = time(NULL);
    delt = (int) t2 - (int) t1;
    if(delt > 0 && (!BATCHPRINT || !(msgs_written % BATCHCOUNT))) { // && msgs_written >= next_cnt) {
      next_cnt *= 2;
      fprintf(stderr,
	      "\r%s: %ld msgs "
	      "(last %dB, %2.2lf MB tot, %d sec, %2.2lf kB/sec, %d:%d:%d errors)  ",
	      domfile, 
	      msgs_written, nbyteswritten, totmb, (int) delt, 
	      (totmb*1024.)/((double) delt),
	      length_errors, contents_errors, readtimeouts);

      if(BATCHPRINT) fprintf(stderr,"\n"); 

    } else {
      //fprintf(stderr,"\r%d messages sent (%d bytes total).",
      //msgs_written, totbytes);
    }
    if(msgs_written >= nummsgs) break;
  }
  fprintf(stderr,
	  "\r%s: %ld msgs "
	  "(last %dB, %2.2lf MB tot, %d sec, %2.2lf kB/sec, %d:%d:%d errors)  ",
	  domfile, 
	  msgs_written, nbyteswritten, totmb, (int) delt, 
	  (totmb*1024.)/((double) delt),
	  length_errors, contents_errors, readtimeouts);
  fprintf(stderr,"\nClosing file.\n");
  close(file);
  fprintf(stderr,"Done.\n");
  
  return 0;
}
