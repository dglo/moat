#!/usr/bin/perl

# Script which duplicates the failure mode Azriel discovered
# Jan., 2004: iceboot produces lots of data and makes the hub
# crash.
# $Id: iceboot_od_test.pl,v 1.1 2005-03-15 00:24:12 jacobsen Exp $

use Fcntl;
use strict;

sub drain_iceboot;   sub step;

my $domdev = shift;
die "Usage: $0 <dom_device_file>\n" unless defined $domdev;
die "Couldn't find DOM device file $domdev" unless -e $domdev;

my $arg = shift;
my $verbose = 1 if $arg eq "verbose" || $arg eq "step";
my $step = 1 if $arg eq "step";

print "open $domdev...\n" if $verbose;
sysopen(DD, $domdev, O_RDWR)
    || die "Can't open $domdev: $!\n";
step;

syswrite DD, "\r\rls\r", 5;
step;

drain_iceboot;
close DD;
print "Closed device, sleeping before open...\n";
sleep 1;
    sysopen(DD, $domdev, O_RDWR)
    || die "Can't open $domdev: $!\n";

drain_iceboot;

# Collect any extra iceboot strings...

print "Draining iceboot...\n" if $step;
drain_iceboot;
step;

print "Sending od command...\n" if $step;
syswrite DD, "\r\r0 100 od\r", 11;
step;

close DD;
print "Closed device, sleeping before open...\n";
sleep 1;
sysopen(DD, $domdev, O_RDWR)
    || die "Can't open $domdev: $!\n";

print "Draining iceboot again...\n" if $step;
drain_iceboot;
step;

print "Doing longer od...\n";
syswrite DD, "\r\r0 8092 od\r", 12;
sleep 10;
drain_iceboot;

print "closing... ";
close DD;

print "$0 $domdev done.\n";

exit;


sub drain_iceboot {
    my $buf;
    while(1) {
	my $itrial;
	my $ntrials = 100;
	for($itrial = 0; $itrial < $ntrials; $itrial++) {
	    my $read = sysread DD, $buf, 4096;
	    if($read > 0) {
		print "$buf";
		print "Read $read bytes ($buf) from DOM.\n" if $verbose;
	        last;
	    }  elsif($itrial == $ntrials-1) {
		print "\n";
		return;
	    } else {
		select undef,undef,undef,0.001;
	    }
	}
    }
}



sub step {
    if($step) {
	print "Press return for the next step...\n";
	<>;
    }
}

__END__

