#!/usr/bin/perl

#
# John Jacobsen, John Jacobsen IT Services, for LBNL/IceCube
# $Id: se.pl,v 1.1 2005-03-14 23:56:01 jacobsen Exp $

use Fcntl;
use strict;
use IO::Select;

sub drain;


my @domdevs;
my %cardof;
my %pairof;
my %domof;
my %nameof;
my %fhof;

sub usage { return <<EOF;
Usage: $0 <dom|all> [dom] ... <sendpat> <expectpat>
       dom is in the form 00a, 00A or /dev/dhc0w0dA
EOF
;}

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

my $max_write_retries = 100;
foreach my $domdev (@domdevs) {
    # print "$domdev\n";
    die "Couldn't find DOM device file $domdev" unless -e $domdev;

    my $dd = anfh; # Anonymous filehandle
    sysopen($dd, $domdev, O_RDWR)
	|| die "Can't open $domdev: $!\n";

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
my $todo = @domdevs;

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
	    print $printable;
	    $dataread{$fname} .= $printable;
	    if($dataread{$fname} =~ /$recvpat/) {
		print " $fname: OK\n";
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
    close $fhof{$domdev};
}

if($todo == 0) {
    print "SUCCESS.\n";
} else {
    print "FAILURE: $todo DOMs did not give expected reply:\n";
    foreach my $fname(@domdevs) {
	if(! $datadone{$fname}) { 
	    print "\t$fname (got $dataread{$fname})\n";
	}
    }
}

exit;

sub drain {
    my $expect = shift;
    my $maxexpect = 1000;
    my $gotsomething = 0;
    my $dataread = "";

    if(defined $expect) {
	for(1..$maxexpect) {
	    my $read = sysread DD, $buf, 4096;
	    if($read > 0) {
		my $printable = $buf; 
		$printable =~ s/\r/\\r/g;
		$printable =~ s/\n/\\n/g;
		print "$printable";
		$dataread .= $buf;
		return 0 if $dataread =~ /$expect/;
	    } else {
		select undef,undef,undef,0.1;
	    }
	}
	return 1; # didn't get it
    }

    # default case:
    for(1..10) {
	my $read = sysread DD, $buf, 4096;
	if($read > 0) {
	    my $printable = $buf; 
	    $printable =~ s/\r/\\r/g;
	    $printable =~ s/\n/\\n/g;
	    print "$printable";
	    $gotsomething++;
	    $dataread .= $buf;
	}
	select undef,undef,undef,0.001;
    }
    if($dataread =~ /error/i) {
	print "Error present on stream from DOM.\n";
	return 1;
    }
    return 0;
}


