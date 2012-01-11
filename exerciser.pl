#!/usr/bin/perl
use Time::HiRes qw(usleep);
use strict;

$| = 1;

my $rounds = 2;
my $steps = 512;
my $min = 14000;
my $max = 42000;

sub linear_interpolate ($$$$$) {
	my ($value, $oldmin, $oldmax, $newmin, $newmax) = @_;
	return ($value - $oldmin) * ($newmax - $newmin) / ($oldmax - $oldmin) + $newmin;
}

for my $f (0..($steps * $rounds)) {
	for my $i (0..15) {
		printf "S %d %d   \n", $i, linear_interpolate(sin(3.1415926535 * 2 * (($f + ($steps * $i / 16)) % $steps) / $steps), -1, 1, $min, $max);
		# usleep 500;
	}
}
