#!/usr/bin/perl
# tracediff.pl - find where two instruction traces diverge.
#
# Usage: tracediff.pl [--context N] [--skip N] <trace-a> <trace-b>
#
# The traces come from the debugger's "trace <file>" command (see
# docs/DEBUGSOCK.md). Each line is
#
#     <pc> <opcode> <r0> <r1> ... <r14> <r15+psr>
#
# all in hex. Two cores running the same software should produce identical
# traces; this prints the first line that differs, says which registers
# disagree, and shows the instructions leading up to it. That is normally
# the whole diagnosis: the divergence is either in that instruction's
# result, or in the one just before it that set up its operands.
#
# Exit code: 0 identical (to the length of the shorter trace), 1 diverged,
# 2 could not read a file.
use strict;
use warnings;
use Getopt::Long;

my $context = 8;
my $skip = 0;
GetOptions('context=i' => \$context, 'skip=i' => \$skip) or die "bad options\n";
my ($fa, $fb) = @ARGV;
die "usage: tracediff.pl [--context N] [--skip N] <trace-a> <trace-b>\n"
	unless defined $fa && defined $fb;

open my $a, '<', $fa or do { print STDERR "$fa: $!\n"; exit 2 };
open my $b, '<', $fb or do { print STDERR "$fb: $!\n"; exit 2 };

my @names = ('pc', 'opcode', map { "r$_" } 0 .. 15);
my @ring;
my $n = 0;

while (1) {
	my $la = <$a>;
	my $lb = <$b>;

	if (!defined $la || !defined $lb) {
		my $which = !defined $la ? $fa : $fb;
		if (defined $la || defined $lb) {
			printf "Traces agree for all %d instructions; %s ends first.\n",
			       $n, $which;
		} else {
			printf "Traces are identical (%d instructions).\n", $n;
		}
		exit 0;
	}
	$n++;
	chomp $la; chomp $lb;
	next if $n <= $skip;

	if ($la ne $lb) {
		my @va = split ' ', $la;
		my @vb = split ' ', $lb;

		printf "Diverged at instruction %d (pc %s)\n\n", $n, $va[0];

		if (@ring) {
			print "Last $context matching instructions:\n";
			print "  $_\n" for @ring;
			print "\n";
		}

		printf "  %s: %s\n", $fa, $la;
		printf "  %s: %s\n\n", $fb, $lb;

		my $max = @va > @vb ? scalar @va : scalar @vb;
		for my $i (0 .. $max - 1) {
			my $x = $va[$i] // '(missing)';
			my $y = $vb[$i] // '(missing)';
			next if $x eq $y;
			printf "  %-6s  %-10s  %s\n", $names[$i] // "field$i", $x, $y;
		}
		exit 1;
	}

	push @ring, $la;
	shift @ring while @ring > $context;
}
