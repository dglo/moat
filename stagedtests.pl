#!/usr/bin/perl

# Staged driver/firmware testing procedure for multiple DOMs/DOR cards
# Jacobsen 2003-2004 Jacobsen IT Svcs for LBNL/IceCube

use strict;
use Getopt::Long;
use constant ENTER       => 13;
use constant ESC         =>  7;
use constant CTRL_L      => 12;
use constant FALLTHRU_OK =>  1;
use constant NOFALLTHRU  =>  2;

sub get_interrupts;
sub show_comm_stats;
my $bindir  = "/usr/local/bin";
my $procdir = "/proc/driver/domhub";

my $nmsgs         = 400000000;
my $ntcals        = 400000000;
my $moni          = 0;
my $kill          = 0;
my $stuffmode     = 1; # 1 for stuffing mode, 0 for non-stuffing-mode 
my $skipid        = 0;
my $skiptcal      = 0;
my $fixsinglepkt;
my $help          = 0;
my $dorfreq;
my $useconfigboot = 0;
my $timedrun;
my $probe         = 0;
my $savetcal      = 0;
my $cpu_timing_interval = 100;
my $testgps       = 0;
my $skipkbchk     = 0;
my $require_kbmin = 40; # Require this many kB/sec during readwrite
my $checkgps      = 1;
my $gpsskip       = 15;
my $gpsticks      = 20000000;

sub usage { return <<EOF;

Usage: $0 [st.in]
          [-h|-help]           Show these options
          [-k|-kill]           Kill current long test jobs
          [-m|-moni]           Show logs for current long test jobs
          [-t|-timedrun <sec>] Runs long test for <sec> seconds
          [-s|-skipid]         Skips fetching of DOM ID
	  [-x|-skiptcal]       Skips carrying out and monitoring of tcalibs
	  [-e|-necho <n>]      Echo n times during long test
          [-c|-ntcals <m>]     Do m tcalibs during long test
	  [-d|-dorfreq <MHz>]  Specify DOR clock frequency (default 10 MHz)
	  [-g|-testgps]        Run GPS test as part of test (10 MHz DOR freq. only)
	  [-p|-probe]          "Probe" for DOMs on power up rather than use st.in.
	  [-v|-savetcal]       Save time calibration data for each channel
	  [-f|-fixsinglepkt <n>] Fix first single pkt length to <n> bytes
	  [-S|-skipkbcheck]    Allow slow connection / skip bandwidth min. check
	  [-b|-useconfigboot]  Use echo-mode-cb rather than echo-mode; 
                               this selects configboot firmware for echo test
                               (if supported by your DOM software release)
st.in should be a file formatted e.g. as:
0 0 A
0 0 B
DONE

EOF
;
}

sub check_log_files;                      sub non_blocking_mode;
sub check_for_running_processes;          sub dochoice;
sub check_doms_comms_status;              sub yes;
sub show_firmware_versions;               sub yn;
sub getkey;                               sub check_card_procs;
sub power_off_modules;                    sub getdomid;
sub driverinstalled;                      sub moni;
sub kill_running_processes;               sub lasterr;
sub clear_lasterr;                        sub softboot;
sub softboot_all;                         sub echo_mode_all;
sub iceboot_all;                          sub reset_comm_stats;
sub test_single_gps;

GetOptions("help|h"          => \$help,
	   "moni|m"          => \$moni,
	   "kill|k"          => \$kill,
	   "timedrun|t=i"    => \$timedrun,
	   "ntcals|c=i"      => \$ntcals,
	   "necho|e=i"       => \$nmsgs,
	   "skipid|s"        => \$skipid,
	   "dorfreq|d=i"     => \$dorfreq,
	   "testgps|g"       => \$testgps,
	   "fixsinglepkt|f=i"=> \$fixsinglepkt,
	   "probe|p"         => \$probe,
	   "savetcal|v"      => \$savetcal,
	   "useconfigboot|b" => \$useconfigboot,
	   "skipkbcheck|S"   => \$skipkbchk,
	   "skiptcal|x"      => \$skiptcal) || die usage;

my $fixsinglepktarg;
$fixsinglepktarg = (defined $fixsinglepkt)? "-p $fixsinglepkt" : "";

my $kbchkarg = $skipkbchk ? "" : "-k $require_kbmin";

die usage if $help;

print "stagedtests.pl\n";
print "DOR-driver testing script by John Jacobsen (jacobsen\@npxdesigns.com) for LBNL/IceCube.\n";

die "No $procdir!  Driver not installed?\n" unless -e $procdir;

$ntcals = 0 if $skiptcal;
print "Will do up to $ntcals time calibrations, $nmsgs echo messages on each DOM.\n";

if($timedrun > 0 && $timedrun < $cpu_timing_interval) {
    $cpu_timing_interval = $timedrun;
}

clear_lasterr;
# skip for now - should work either blocking or nonblocking 
# non_blocking_mode;

my %card;
my %pair;
my %dom;
my $i=0;
my $fallthrough=0;

if($kill) {
    print "Killing all running test processes...\n";
    kill_running_processes;
    exit;
}

if(!$moni) {
    check_for_running_processes;
}

while(!$moni && !defined $dorfreq) {
    print "You must specify DOR clock speed: [1]0 MHz or (2)0 MHz (or (q)uit)? ";
    my $key = getkey; print chr($key)."\n";
    if($key == ENTER || $key == ord('2')) {
	$dorfreq = 20;
    } elsif($key == ord('1')) {
	$dorfreq = 10;
    } elsif($key == ord('q')) {
	exit;
    }
}

die "Can only test GPS if DOR freq. is 10 MHz!\n" if($dorfreq != 10 && $testgps);

if($testgps) {
    dochoice("[t]est acquire single GPS time string/DOR time pair", 't', FALLTHRU_OK, 
	     \&test_single_gps);
}

if(!$moni) {    
    dochoice("[p]ower off modules", 'p', FALLTHRU_OK, \&power_off_modules);
    dochoice("power [o]n modules", 'o', FALLTHRU_OK, \&power_on_modules);
}

my $inputfile = shift;
if($probe) {
    if(defined $inputfile) { 
	print "WARNING: input file given with -p option,\n"
	    . "will ignore $inputfile.\n";
    }
    my @pfs = </proc/driver/domhub/card*/pair*/dom*/is-communicating>;
    for(@pfs) {
	m|/proc/driver/domhub/card(\d+)/pair(\d+)/dom(\S)/|;
	my $card = $1; my $pair = $2; my $dom = $3;
	if(`cat $_` =~ /is communicating/) {
	    $card{$i} = $card;
	    $pair{$i} = $pair;
	    $dom{$i}  = $dom;
	    $i++;
	}
    }
} else {
    if(!defined $inputfile) {
	$inputfile = "st.in";
    }
    die "No input file $inputfile, quitting.\n" unless -f $inputfile;
    open IF, "$inputfile" || die "Can't open $inputfile: $!\n";
    
    while((my $str = <IF>) !~ /DONE/) {
	chomp $str;
	if($str =~ /^(\s?)#/) {
	   next;
       }
	if($str !~ /(\d+) (\d+) (A|B)/) {
	    print "Bad DOM specifier in $inputfile: $!\n";
	    exit;
	} else {
	    $card{$i} = $1;
	    $pair{$i} = $2;
	    $dom{$i}  = $3;
	    $i++;
	}
    };
    close IF;
}

my $ndoms = $i;
my %ison;

if($ndoms == 0) {
    print "No DOMs selected, quitting.\n";
    exit;
}

if($moni) {
    moni;
    exit;
}

print "Will test:\n";
for(keys %dom) {
    print "$_: Card $card{$_} pair $pair{$_} dom $dom{$_}.\n";
}

check_card_procs;
reset_comm_stats;
show_comm_stats("commstats_before");

print "Firmware versions:\n";
print show_firmware_versions;

check_doms_comms_status;

dochoice("put modules in [i]ceboot", 'i', FALLTHRU_OK, \&iceboot_all);

dochoice("soft[b]oot all modules", 'b', FALLTHRU_OK, \&softboot_all);

dochoice("Show DOM (i)d numbers", 'i', FALLTHRU_OK, sub {
    for($i=0; $i<$ndoms; $i++) {
	my $id = getdomid($i);
	if(defined $id) {
	    print "$card{$i} $pair{$i} $dom{$i}: Good ID ($id).\n";
	} else {
	    print "Failed to get DOM ID for $card{$i} $pair{$i} $dom{$i}.\n";
	    exit;
	}
    }
}) unless $skipid;

# dochoice("Perform [e]cho-mode / softboot tests", 'e',  FALLTHRU_OK, sub { 
#     echo_mode_all;
#     softboot_all;
#     echo_mode_all;
#     softboot_all;
#     echo_mode_all;
#     softboot_and_echo_all;
#     softboot_and_echo_all;
#     softboot_and_echo_all;
# });

dochoice("Put all DOMs into echo [m]ode", 'm', FALLTHRU_OK, sub { echo_mode_all; });

my %devfiles;
my %tprocfiles;
for($i=0; $i<$ndoms; $i++) {
    $devfiles{$i}   = "/dev/dhc".$card{$i}."w".$pair{$i}."d".$dom{$i};
    $tprocfiles{$i} = "/proc/driver/domhub/card".$card{$i}."/pair".$pair{$i}.
	"/dom".$dom{$i}."/tcalib";
}

dochoice("[e]cho-test individual channels (1 msg)", 'e', FALLTHRU_OK, sub {
    for($i=0; $i<$ndoms; $i++) {
	# print `cat /proc/driver/domhub/card$card{$i}/pair$pair{$i}/dom$dom{$i}/comstat`;
	my $echocmd = "$bindir/readwrite HUB $fixsinglepktarg $devfiles{$i} 1 2>&1";
	print "$echocmd\n";
	my $echoresult = `$echocmd`;
	if($echoresult =~ /Closing file.\nDone.$/m) {
	    print "$devfiles{$i} passes single-message echo test.\n\n";
	} else {
	    print "$devfiles{$i} failed single-message echo test:\n$echoresult\n";
	    print "comstat: \n";
	    print `cat /proc/driver/domhub/card$card{$i}/pair$pair{$i}/dom$dom{$i}/comstat`;
	    die "$devfiles{$i} failed single-message echo test.\n";
	}	
    }
});

dochoice("Do single [t]ime calib on each channel", 't', FALLTHRU_OK, sub {
    for($i=0; $i<$ndoms; $i++) {
	my $tcalresult = `$bindir/tcaltest -d $dorfreq -t 10000 $tprocfiles{$i} 1 noshow 2>&1`;
	if($tcalresult !~ /FAILED/) {
	    print "$tprocfiles{$i} PASSED.\n";
	} else {
	    die "$tprocfiles{$i} FAILED!  Session text:\n$tcalresult\n\n";
	}
    }
}) unless $skiptcal;

my $longjobs = 0;
my $ifgps = $testgps? "/GPS readout" : "";
dochoice("Start [l]ong-term echo/tcalib$ifgps tests", 'l', FALLTHRU_OK, sub {
    for($i=0; $i<$ndoms; $i++) {
	my $echoout = "echo_results_c$card{$i}"."w$pair{$i}"."d$dom{$i}.out";
	my $tcalout;
	my $tcaldata;
	$tcalout = "tcal_results_c$card{$i}"."w$pair{$i}"."d$dom{$i}.out";
	if($savetcal) { 
	    $tcaldata = "tcal_data_c$card{$i}"."w$pair{$i}"."d$dom{$i}.out";
	} else {
	    $tcaldata = "/dev/null";
	}
	my $rwcmd = "$bindir/readwrite HUB $kbchkarg $devfiles{$i} ".($stuffmode?"-s":"").
	    " $nmsgs >& $echoout &";
	my $tccmd = "$bindir/tcaltest  -d $dorfreq  $tprocfiles{$i} $ntcals "
	    .($savetcal?"":"noshow")." 2>$tcalout 1>$tcaldata &";
	if($nmsgs > 0) {
	    print "Running $rwcmd...\n";
	    system $rwcmd;

	}
	if($ntcals > 0 && ! $skiptcal) {
	    print "Running $tccmd...\n";
	    system $tccmd;
	}
    }
    if($testgps) {
	my @pfs = </proc/driver/domhub/card*/syncgps>;
	for(@pfs) {
	    m|/proc/driver/domhub/card(\d+)/syncgps|;
	    my $fout = "card$1_gps.out";
	    my $chk = ($checkgps ? "-e $gpsskip,$gpsticks" : "");
	    my $gpscmd = "/usr/local/bin/readgps -d $_ >&$fout $chk &";
	    print "Running $gpscmd...\n";
	    system $gpscmd;
	}
    }
    $longjobs = 1;
});

sub get_interrupts {

#            CPU0       CPU1
#   0:   43128503   43074691    IO-APIC-edge  timer
#   1:          1          2    IO-APIC-edge  keyboard
#   2:          0          0          XT-PIC  cascade
#   8:          1          0    IO-APIC-edge  rtc
#   9:          2          0   IO-APIC-level  ohci1394
#  11:   13450700   13520695   IO-APIC-level  dpti0, eth0
#  14:          0          2    IO-APIC-edge  ide0

    my @dhlines = `cat /proc/interrupts`;
    my $sumint = 0;
    for(@dhlines) {
	chomp;
	if(/^\s*\d+:\s+(\d+)/) {    # Find good lines
	    # print;
	    s/^\s+\d+:\s+//;        # strip off leading whitespace & IRQ
	    my @toks = split /\s+/;
	    # Sum interrupts for all CPUs 
	    # until non-decimal-number token (interrupt type)
	    my $thissum = 0;
	    my $tok;
	    while($tok = shift @toks) {
		if($tok =~ /^\d+$/) { 
		    $thissum += $tok;
		} else {
		    last;
		}
	    }
	    # print "... sum $thissum: ";
	    # Find devices, look for dh
	    my $isdriver = 0;
	    while($tok = shift @toks) {
		# print "[$tok]";
		if($tok =~ /^dh,?$/) {
		    $isdriver = 1;
		    $sumint += $thissum;
		}
	    }
	    # print $isdriver? "DRIVER" : "not driver";
	    # print "\n";
	}
    }
    warn "No interrupts found for DOR-driver!\n" unless $sumint;
    return $sumint;
}

if($timedrun) {
    my $now = time;
    print "Started run at ".(scalar localtime)."\n";
    my $int0 = get_interrupts;
    print "Calculating system CPU load (monitoring for $cpu_timing_interval seconds)...: ";
    my $result = `vmstat $cpu_timing_interval 2 | tail -1 | awk '{print \$15}'`;
    chomp $result;
    print "$result\%.\n";
    my $int1 = get_interrupts;
    if($int1) { # Don't report interrupts unless some were found 2nd time
	my $rate = ($int1 - $int0) / $cpu_timing_interval;
	printf "Interrupt rate = %2.2f Hz.\n", $rate;
    }
    while(1) {
	my $later = time;
	if($later - $now > $timedrun) {
	    my $lt = scalar localtime;
	    print "End of run at $lt.  Killing jobs...\n";
	    kill_running_processes;
	    sleep 1;
	    show_comm_stats("commstats_after");
	    print "Showing last log files now....\n";
	    if(check_log_files) {
		print "Stagedtests.pl FAILED.\n";
	    } else {
		print "Stagedtests.pl: SUCCESS.\n";
	    }
	    exit;
	}
	sleep 1;
    }
} elsif($longjobs) {
    moni;
}

exit;

#### END OF MAIN


sub moni {
    while(1) {
	dochoice("Show [l]ast line of long term tests", 'l', NOFALLTHRU, \&check_log_files);
    }
}

sub check_for_running_processes {
# Check for readwrite or tcaltest
    while(1) {
	my @ps = `ps --columns 1000 ax`;
	my $foundAlien = 0;
	for(@ps) {
	    if ((/readwrite HUB/ || (! $skiptcal && /tcaltest/)) && !/emacs/) {
		print STDERR "$_";
		$foundAlien++;
	    }
	}
	last unless $foundAlien;
	if($timedrun) { # just get out, not running interactively
	    die "Found already running test process(es), quitting.\n";
	} else {
	    if(yes(yn("Found at least one already running test process, kill all? "))) {
		kill_running_processes;
	    } else {
		die "Ok, quitting.\n";
	    }
	}
    }
}

sub killall {
    # Send SIGINT to all processes matching argument; 
    # make sure they're really dead.
    my $argname = shift;
    my $done = 0;
    my $numkilled = 0;
    foreach my $trial (0..100) {
	my $hadone = 0;
	my @ps = `ps --columns 1000 ax`;
	for(@ps) {
	    if(m|/usr/local/bin/$argname|) {
		$hadone++;
		my $pid = (split " ")[0];
		my $killed = kill ('TERM', $pid);
		die "Couldn't kill process $pid: $!\n" unless $killed == 1;
		$numkilled++;
	    }
	}
	if(!$hadone) {
	    $done = 1;
	    last;
	}
	select undef,undef,undef,0.2;
	print "\n";
    }
    die "Couldn't kill all processes matching \"$argname\"...\n" unless $done;	
}

sub kill_running_processes {
    killall "readwrite";
    killall "tcaltest" unless $skiptcal;
    killall "readgps";
}

sub yn {
    my $question = shift;
    print $question ." ";
    my $data = <STDIN>;
    return $data;
}

sub yes {
    my $pat = shift;
    return ($pat=~/^y/i)?1:0;
}

sub comms_reset { 
    my $dom = shift;
    my $pf = "/proc/driver/domhub/card$card{$i}/pair$pair{$i}/dom$dom{$i}/is-communicating";
    print "Comms. reset: $pf....\n";
    open PF, ">$pf"
	|| die "Can't open $pf: $!\n";
    print PF "reset";
    close PF;
}       


sub check_doms_comms_status {
    my $ok = 1;
    for(keys %dom) {
	my $isresp = `cat $procdir/card$card{$_}/pair$pair{$_}/dom$dom{$_}/is-communicating`;
	print $isresp;
	if($isresp !~ /is communicating/) {
	    $ok = 0;
	}
    }
    die "One or more channels not responding, TEST FAILED.\n" unless $ok;
}

sub show_firmware_versions {
    my $resp = "";
    my @cards = </proc/driver/domhub/card*>;

    $resp .= "Driver ".`cat /proc/driver/domhub/revision`;

    my $fw;
    foreach my $card(@cards) {
        my @fpga = `cat $card/fpga`;
        for(@fpga) {
            if(/FREV/) {
                if(/FREV\s+0x000(\w)(\w\w)(\w\w)/) {
                    $fw = "$1$2".chr(hex($3));
                    $resp .= "$card -- $fw\n";
                } else {
                    $resp .= "$card -- Corrupt FREV: $_.\n";
                }
            }
        }
    }
    return $resp;
}

sub getkey {
    use IO::Select;
    use POSIX;
    
# Stuff for char-based terminal io (perlterm.pl)    
    my $fdterm;
    my ($tterm, $sterm); # Terminal properties
    $fdterm = POSIX::open("/dev/tty",O_RDWR);
    $tterm  = new POSIX::Termios;
    $sterm  = new POSIX::Termios;
    $tterm->getattr($fdterm);
    $sterm->getattr($fdterm);
    my $oflag = $tterm->getlflag();
    $tterm->setiflag(0);
    $tterm->setlflag(0);
    $tterm->setcc(VTIME,0);
    $tterm->setcc(VMIN,0);
    $tterm->setattr($fdterm,TCSANOW);

    
    select STDOUT; $|++; # Otherwise Win32 stdout gets screwed up
    
# New main

    my $select = IO::Select->new();
    $select->add($fdterm);
    my $b;
    while(1) {
	my @readList = $select->can_read(10);
	my $nready = @readList;
	if((scalar @readList) > 0 && $readList[0] == $fdterm) {
	    my $nread = POSIX::read($fdterm, $b, 1);
	    if($nread > 0) {
		$tterm->setlflag($oflag);
		$tterm->setcc(VTIME, 0);
		$tterm->setattr($fdterm, TCSANOW);
		POSIX::close $fdterm;
		return ord($b);
	    }	
	}
	select undef,undef,undef,0.01;
    }
}

sub getresp {
    my $string = shift;
    print $string."...";
    my $key = getkey;
    print "\n";
    return $key;
}

sub power_off_modules {
    print "Switching off ALL modules...\n";
    system "echo off > /proc/driver/domhub/pwrall";
    sleep 1;
}

sub power_on_modules {
    print "Switching on ALL modules...\n";
    system "echo on > /proc/driver/domhub/pwrall";
}

sub dochoice {
    my $prompt      = shift;
    my $action_char = shift;
    my $fallthruopt = shift;
    my $subref      = shift;
    my $choice;
    print "\n";
    if(($fallthruopt == FALLTHRU_OK && $fallthrough) || defined $timedrun) {
	print "AUTO($prompt)...\n";
	&$subref;
	return;
    }
    while(1) {
	$choice = getresp($prompt."  (s)kip  "
			  .($fallthruopt == FALLTHRU_OK ? "(A)ll  ":"")."(q)uit");
	if($choice == ENTER || $choice == ord($action_char)) {
	    &$subref;
	    last;
	} elsif($fallthruopt == FALLTHRU_OK && $choice == ord('A')) {
	    &$subref;
	    $fallthrough = 1;
	    last;
	} elsif($choice == ord('s')) {
	    print "Skipping....\n";
	    last;
	} elsif($choice == ord('q') || $choice == ESC) {
	    exit;
	} elsif($choice == CTRL_L) {
	    system "clear";
	} else {
	    print "Invalid key ($choice).\n";
	}
    }
}
    
sub check_card_procs {
    for(keys %dom) {
	my $cardproc = "$procdir/card$card{$_}";
	if(! -e $cardproc) {
	    print "Proc file $cardproc missing, probably not installed.\n";
	    print "Check input file \"$inputfile\".\n";
	    exit;
	}
    }
}

sub getdomid {
    my $i = shift;
    my $idproc = "/proc/driver/domhub/card".$card{$i}."/pair".$pair{$i}.
	"/dom".$dom{$i}."/id";
    my $id = `cat $idproc`; chomp $id;
    if($id =~ /Card $card{$i} Pair $pair{$i} DOM $dom{$i} ID is (\w+)$/) {
	return $1;
    } else {
	print "ID request to $idproc failed ($id)\n";
	return undef;
    }
}


sub lasterr { 
    return `cat $procdir/lasterr`;
}


sub lasterr_ok {
    my $le = shift;
    return 0 unless defined $le;
    return 0 if $le !~ /^0: no error/;
    return 1;
}

sub clear_lasterr {
    my $le = lasterr; # Clear last error proc file
    if(!lasterr_ok($le)) {
        warn "Cleared proc file but there was an error previously: \n$le\n";
    }
}

sub softboot_all {
    my $i;
    my $sbcmd = "$bindir/sb.pl ";
    for($i=0; $i<$ndoms; $i++) {
	$sbcmd .= $card{$i}.$pair{$i}.$dom{$i}." ";
    }
    my $tf = "/tmp/st$$"."_sb.tmp";
    system "$sbcmd 2>&1 | tee $tf";
    my $result = `cat $tf`;
    unlink $tf;
    if($result !~ /ok/i) {
	print "Softboot failed.  Later.\n";
	print "stagedtests FAILURE.\n";
	exit;
    }
}


sub iceboot_all {
    my $i;
    my $secmd = "$bindir/se.pl ";
    for($i=0; $i<$ndoms; $i++) {
        $secmd .= $card{$i}.$pair{$i}.$dom{$i}." ";
    }
    $secmd .= "r r.+\\>";
    print "$secmd...\n";
    my $tf = "/tmp/st$$"."_se.tmp";
    system "$secmd 2>&1 | tee $tf";
    my $result = `cat $tf`;
    unlink $tf;
    if($result !~ /success/i) {
        print "Iceboot mode change failed.  Sorry.\n";
        print "stagedtests FAILURE.\n";
        exit;
    }
    sleep 2; # Give DOM time to reboot before softbooting
}

sub echo_mode_all {
# Put DOMs in echo mode (if not already)
    my $i;
    my $seprecmd = "$bindir/se.pl ";
    for($i=0; $i<$ndoms; $i++) {
        $seprecmd .= $card{$i}.$pair{$i}.$dom{$i}." ";
    }
    if($useconfigboot) {
	# Start configboot firmware on DOM
	my $emstr = "s\\\"\\\ configboot.sbi\\\"\\\ find\\\ if\\\ fpga\\\ endif s\\\".+\\\>";
	my $secmd = $seprecmd . $emstr;
	print "$secmd\n";
	my $tf = "/tmp/st$$"."_se.tmp";
	system "$secmd 2>&1 | tee $tf";
	my $result = `cat $tf`;
	unlink $tf;
	if($result !~ /success/i) {
	    print "Load of configboot firmware failed: $result.  Sorry.\n";
	    print "stagedtests FAILURE.\n";
	    exit;
	}
    }

    my $emstr = "echo-mode";
    my $secmd = $seprecmd."$emstr $emstr";
    print "$secmd\n";
    my $tf = "/tmp/st$$"."_se.tmp";
    system "$secmd 2>&1 | tee $tf";
    my $result = `cat $tf`;
    unlink $tf;
    if($result !~ /success/i) {
        print "Change to echo mode failed.  Sorry.\n";
        print "stagedtests FAILURE.\n";
        exit;
    }
}


sub check_log_files {
    my $i;
    my $retval = 0;
    if(! $skipid) {
	for($i=0; $i<$ndoms; $i++) {
	    my $id = getdomid($i);
	    if(defined $id) {
		print "$card{$i} $pair{$i} $dom{$i}: Good ID ($id).\n";
	    } else {
		print "Failed to get DOM ID for $card{$i} $pair{$i} $dom{$i}.\n";
		print "stagedtests FAILURE.\n";
		$retval = 1;		    
	    }
	}
    }
    if($nmsgs > 0) {
	for($i=0; $i<$ndoms; $i++) {
	    my $echoout = "echo_results_c$card{$i}"."w$pair{$i}"."d$dom{$i}.out";
	    my $tail = `tail -1 $echoout`;
	    print $tail;
# /dev/dhc0w0dA: 1000 msgs (last 188B, 1.19 MB tot, 27.11 sec, 45.86 kB/sec, ARR=0)
	    if($tail !~ m|/dev/dhc\d+w\d+d\S: \d+ msgs \(last|) {
		print "stagedtests FAILURE.\n";
		$retval = 1;
	    }
	}
    }
    if($ntcals > 0 && ! $skiptcal) {
	for($i=0; $i<$ndoms; $i++) {
	    my $tcalout = "tcal_results_c$card{$i}"."w$pair{$i}"."d$dom{$i}.out";
	    my $tail = `tail -1 $tcalout`;
	    print $tail;
# /proc/driver/domhub/card0/pair0/domB/tcalib: 2380 tcals, 0 rdtimeouts, 0 wrtimeouts.
	    if($tail !~ m|/proc/driver/domhub/card\d+/pair\d+/dom\S/tcalib: \d+ tcals|) {
		print "stagedtests FAILURE.\n";
		$retval = 1;
	    }
	}
    }
    if($testgps) {
	my @pfs = </proc/driver/domhub/card*/syncgps>;
	for(@pfs) {
	    m|/proc/driver/domhub/card(\d+)/syncgps|;
	    my $outfile = "card$1_gps.out";
	    my $data = `tail -1 $outfile`;
# GPS 320:19:31:11 TQUAL(' ' exclnt.,<1us) DOR 0000000056bd138c
	    if($data !~ /GPS.+?TQUAL.+?DOR/ || $data =~ /fail/i) {
		print "$outfile: $data\n";
		print "stagedtests FAILURE.\n";
		return 1;
	    }
	}
    }

    return $retval;
}

sub non_blocking_mode {
    system "echo 0 > /proc/driver/domhub/blocking";
    print `cat /proc/driver/domhub/blocking`;
}

sub reset_comm_stats {
    for(keys %dom) {
        my $pf = "/proc/driver/domhub/card$card{$_}/pair$pair{$_}/dom$dom{$_}/comstat";
        die "Proc file $pf not found!\n" unless -e $pf;
	system "echo reset > $pf";
    }
}

sub show_comm_stats {
    my $file = shift; return unless defined $file;
    open F, ">$file";
    for(keys %dom) {
	my $pf = "/proc/driver/domhub/card$card{$_}/pair$pair{$_}/dom$dom{$_}/comstat";
	die "Proc file $pf not found!\n" unless -e $pf;
	print F `cat $pf`;
    }
    close F;
}

sub test_single_gps {
    my @pfs = </proc/driver/domhub/card*/syncgps>;
    for(@pfs) {
	m|/proc/driver/domhub/card(\d+)/syncgps|;
	print "Card $1: ";
	my $result = `/usr/local/bin/readgps -o $_ 2>&1`;
# GPS 320:19:31:11 TQUAL(' ' exclnt.,<1us) DOR 0000000056bd138c
	print $result;
	if($result !~ /GPS.+?TQUAL.+?DOR/) {
	    print "Stagedtests FAILED.\n";
	    exit;
	}
    }
}
