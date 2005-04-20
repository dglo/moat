#!/usr/bin/perl

#
# John Jacobsen, John Jacobsen IT Services, for LBNL/IceCube
# $Id: sb.pl,v 1.1 2005-03-14 23:56:01 jacobsen Exp $

use Fcntl;
use strict;
use POSIX "sys_wait_h";

sub drain;

my @domdevs;
my %cardof;
my %pairof;
my %domof;
sub usage { return <<EOF;
Usage: $0 <dom|all> [dom] ...
       dom is in the form 00a, 00A or /dev/dhc0w0dA
EOF
;
	}

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

die usage unless @domdevs > 0;

sub lasterr { return `cat /proc/driver/domhub/lasterr`; }
lasterr; # Clear it
my %pf;
my %abbrev;

foreach my $dom (@domdevs) {
    my $card = $cardof{$dom};
    my $pair = $pairof{$dom};
    my $aorb = $domof{$dom};
    $pf{$dom} = "/proc/driver/domhub/card$card/pair$pair/dom$aorb/softboot";
    die "DOM, card, or driver not found (no $pf{$dom})!\n" unless -f $pf{$dom};
    $abbrev{$dom} = "$card$pair$aorb";
}

my %pid;
foreach my $dom (@domdevs) {
    print "$abbrev{$dom} ";
    my $pid = fork;
    die "Can't fork (no mem?)\n" unless defined $pid;
    if($pid > 0) {
	$pid{$dom} = $pid;
	# print "$pid\n";
    } else { # You're the kid: do something
	open PF, ">$pf{$dom}" || die "Can't open $pf{$dom}: $!\n";
	print PF "reset\n";
	close PF;
	exit;
    }
}

wait;
my $le = lasterr;
if($le =~ /^0:/) { 
    print "Ok.\n";
} else {
    print "Error $le\n";
    print "Don't know which DOM it was - run $0 on each channel individually to find out.\n";
}



