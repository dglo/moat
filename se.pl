#!/usr/bin/perl

#
# John Jacobsen, NPX Designs, Inc. for LBNL/IceCube
# 
# Send data to multiple DOMs and look for correct response
#
# $Id: se.pl,v 1.6 2006-03-31 22:50:53 jacobsen Exp $

use Fcntl;
use strict;
use IO::Select;
use Getopt::Long;

my @domdevs;
my %cardof;
my %pairof;
my %domof;
my %nameof;
my %fhof;
my $suppress;
my $help;
sub usage { return <<EOF;
Usage: $0 [-s] <dom|all> [dom] ... <sendpat> <expectpat>
       dom is in the form 00a, 00A or /dev/dhc0w0dA
       -s: suppress all but matching pattern output
EOF
;}

GetOptions("help|h"    => \$help,
	   "s"         => \$suppress) || die usage;

die usage if $help;

my $recvpat = pop @ARGV; die usage unless defined $recvpat;
my $sendpat = pop @ARGV; die usage unless defined $sendpat;

sub anfh { local *FH; return *FH; }

if($ARGV[0] eq "all") {
    my @iscoms = </proc/driver/domhub/card*/pair*/dom*/is-communicating>;
    foreach my $pf (@iscoms) {
	my $res = `cat $pf`;
	if($res =~ /Card (\d+) Pair (\d+) DOM (\S+) is communicating/i) {
	    my $dev = "/dev/dhc$1"."w$2"."d$3";
	    push @domdevs, $dev;
	    $cardof{$dev} = $1;
	    $pairof{$dev} = $2;
	    $domof{$dev}  = $3;
	}
    }
    die "No communicating DOMs - check power and/or hardware.\n" 
	unless @domdevs;
} else {
    # Loop over DOM arguments
    foreach my $domarg (@ARGV) {
	if($domarg =~ /^(\d)(\d)(\w)$/) {
	    my $dom  = $3;
	    $dom     =~ tr/[a-z]/[A-Z]/;
	    my $dev = "/dev/dhc$1"."w$2"."d$dom";
	    push @domdevs, $dev;
	    $cardof{$dev} = $1;
	    $pairof{$dev} = $2;
	    $domof{$dev}  = $dom;
	} elsif($domarg =~ /^\/dev\/dhc(\d)w(\d)d(\w)$/) {
	    $cardof{$domarg} = $1;
	    $pairof{$domarg} = $2;
	    $domof{$domarg}  = $3;
	    push @domdevs, $domarg;
	} else {
	    die "Unknown DOM label $domarg!\n";
	}
    }
}

die "No DOMs specified!\n".usage unless @domdevs > 0;

$sendpat = "$sendpat\r";

my $buf;

$|++;

my $selector = IO::Select->new();

my %openfail;

my $max_write_retries = 100;
foreach my $domdev (@domdevs) {
    # print "$domdev\n";
    die "Couldn't find DOM device file $domdev" unless -e $domdev;

    my $dd = anfh; # Anonymous filehandle
    if(!sysopen($dd, $domdev, O_RDWR)) {
	warn "WARNING: open failed on $domdev!\n";
	$openfail{$domdev} = 1;
	next;
    } 

    $selector->add($dd);
    my $towrite = length($sendpat);
    my $i;
    my $wrote = 0;
    for($i=0;$i<$max_write_retries;$i++) {
	$wrote = syswrite $dd, "$sendpat"; # dd must be immediately writeable
	last if $wrote > 0;
	select undef,undef,undef,0.01;
    }
    die "Couldn't successfully write to $domdev (after $max_write_retries trials).\n"
	unless $wrote == $towrite;
    
    $nameof{fileno($dd)} = $domdev;
    $fhof{$domdev} = $dd;
}

my %reply;
# Wait for data from each
my @ready;
my %dataread;
my %datadone;
my $todo = @domdevs - scalar keys %openfail;;

my $now = time;
while(abs(time - $now) < 10) {
    @ready = $selector->can_read(1);
    my $n = @ready;
    if(@ready) {
        # print "Can read from ".(scalar @ready)." devices.\n";
	foreach my $fh (@ready) {
	    my $fname = $nameof{fileno($fh)};
	    my $read = sysread $fh, $buf, 4096;
	    my $printable = $buf;
	    $printable =~ s/\r/\\r/g;
	    $printable =~ s/\n/\\n/g;
	    print $printable unless $suppress;
	    $dataread{$fname} .= $printable;
	    if($dataread{$fname} =~ /$recvpat/) {
		if(defined $1) {
		    print " $fname: OK ($1).\n";
		} else {
		    print " $fname: OK.\n";
		}
		$selector->remove($fh);
		$datadone{$fname} = 1;
		$todo--;
	    }
	}
    }
    last if($todo == 0);
}

# Close each
foreach my $domdev (@domdevs) {
    # print "Close $domdev: ";
    close $fhof{$domdev} unless $openfail{$domdev};;
}

if($todo == 0 && (keys %openfail) == 0) {
    print "SUCCESS.\n";
} else {
    print "FAILURE:\n";
    foreach my $fname(@domdevs) {
	if($openfail{$fname}) {
	    print "\t$fname failed to open.\n";
	} elsif(! $datadone{$fname}) {
	    if($dataread{$fname} eq "") {
		print "\t$fname got NO DATA.\n"; 
	    } else {
		print "\t$fname (got $dataread{$fname})\n";
	    }
	}
    }
}


