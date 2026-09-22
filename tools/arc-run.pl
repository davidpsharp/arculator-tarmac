#!/usr/bin/perl
# arc-run.pl - run a RISC OS *command inside Arculator over HostCmd.
#
# Usage: arc-run.pl [--port N] [--host H] [--timeout S] [--quiet] -- <command...>
#        arc-run.pl [--port N] --wait [--timeout S]
#
# Prints the command's output, and exits with its RISC OS return code
# (Sys$ReturnCode), 255 if the guest never ran it, or 254 if the emulator
# could not be reached. --wait just waits until the HostCmd port answers,
# for scripts that have just launched the emulator. Notices from the
# emulator (the greeting, "machine reset", ...) go to stderr unless --quiet.
#
# Protocol: one '\n'-terminated command line; replies are frames
# [type:1][len:u32 BE][payload], type 'O' output, 'D' done (payload = 4-byte
# BE return code), 'X' notice. See docs/HOSTCMD.md.
use strict;
use warnings;
use IO::Socket::INET;
use Getopt::Long qw(:config no_ignore_case pass_through);

my $host = '127.0.0.1';
my $port = 15600;
my $timeout = 120;
my $quiet = 0;
my $wait = 0;
GetOptions('host=s' => \$host, 'port=i' => \$port, 'timeout=i' => \$timeout,
           'quiet' => \$quiet, 'wait' => \$wait) or die "bad options\n";
shift @ARGV if @ARGV && $ARGV[0] eq '--';
my $command = join(' ', @ARGV);
die "usage: arc-run.pl [--port N] [--timeout S] [--quiet] -- <command>\n"
	unless $wait || length $command;

my $deadline = time + $timeout;
my $sock;
do {
	$sock = IO::Socket::INET->new(PeerHost => $host, PeerPort => $port,
	                              Proto => 'tcp', Timeout => 2);
	if (!$sock) {
		if (!$wait || time >= $deadline) {
			print STDERR "arc-run: cannot connect to $host:$port: $@\n";
			exit 254;
		}
		select(undef, undef, undef, 0.5);
	}
} until $sock;
$sock->autoflush(1);
binmode $sock;
binmode STDOUT;

if ($wait && !length $command) {
	# Read the greeting so we know the emulator is really answering.
	my $hdr = read_exact($sock, 5);
	exit($hdr ? 0 : 254);
}

print $sock "$command\n";

my $rc = 255;
while (1) {
	my $hdr = read_exact($sock, 5);
	if (!defined $hdr) {
		print STDERR "arc-run: connection closed before the command finished\n";
		last;
	}
	my ($type, $len) = unpack('a N', $hdr);
	my $payload = $len ? read_exact($sock, $len) : '';
	last unless defined $payload;
	if ($type eq 'O') {
		print $payload;
	} elsif ($type eq 'D') {
		$rc = unpack('N', $payload);
		last;
	} elsif ($type eq 'X') {
		print STDERR $payload unless $quiet;
	} else {
		print STDERR "arc-run: unknown frame type '$type'\n";
	}
}
close $sock;
$rc = 255 if $rc == 0xffffffff;
exit($rc & 0xff);

sub read_exact {
	my ($s, $n) = @_;
	my $buf = '';
	while (length($buf) < $n) {
		if (time >= $deadline) {
			print STDERR "arc-run: timed out after ${timeout}s\n";
			return undef;
		}
		my $r = sysread($s, my $chunk, $n - length $buf);
		return undef if !defined $r || $r == 0;
		$buf .= $chunk;
	}
	return $buf;
}
