#!/usr/bin/perl
# arc-debug.pl - drive Arculator's debugger over the DebugSock socket.
#
# Usage: arc-debug.pl [--port N] [--host H] [--quiet-ms N] [--timeout S] \
#                     [-- <command> [<command> ...]]
#        arc-debug.pl [--port N]            # interactive
#
# Each argument after -- is one debugger command line, sent in order; the
# output of each is printed. With no commands the script is interactive:
# type debugger commands, Ctrl-D (or "detach") to leave.
#
# While the machine is running the only commands the socket accepts are
# "pause", "detach" and "help"; everything else needs a paused machine (see
# docs/DEBUGSOCK.md). "pause" is therefore usually the first command.
#
# The debugger's output has no framing - it is the same text the console
# window shows - so a reply is taken to have ended once the socket has been
# quiet for --quiet-ms (default 400 ms).
use strict;
use warnings;
use IO::Socket::INET;
use IO::Select;
use Getopt::Long qw(:config no_ignore_case pass_through);

my $host = '127.0.0.1';
my $port = 15601;
my $quiet_ms = 400;
my $timeout = 60;
GetOptions('host=s' => \$host, 'port=i' => \$port,
           'quiet-ms=i' => \$quiet_ms, 'timeout=i' => \$timeout)
	or die "bad options\n";
shift @ARGV if @ARGV && $ARGV[0] eq '--';

my $sock = IO::Socket::INET->new(PeerHost => $host, PeerPort => $port,
                                 Proto => 'tcp', Timeout => 5)
	or die "arc-debug: cannot connect to $host:$port: $@\n";
$sock->autoflush(1);
binmode $sock;
my $sel = IO::Select->new($sock);

print drain();

if (@ARGV) {
	for my $cmd (@ARGV) {
		print "> $cmd\n";
		print send_command($cmd);
	}
} else {
	print "Interactive. Ctrl-D to leave (the machine keeps running).\n";
	while (defined(my $line = <STDIN>)) {
		chomp $line;
		print send_command($line);
	}
}
close $sock;
exit 0;

sub send_command {
	my ($cmd, $no_retry) = @_;
	print $sock "$cmd\n";
	my $out = drain();

	# Each connection leaves the machine running when it closes, so a script
	# would otherwise have to open with "pause" every time. Do it for them,
	# once, rather than making the first command silently do nothing.
	if (!$no_retry && $out =~ /machine is running - send 'pause' first/
	    && $cmd !~ /^\s*(pause|p|detach|help|\?)\s*$/i) {
		print "[auto-pausing]\n";
		send_command('pause', 1);
		$out = send_command($cmd, 1);
	}
	return $out;
}

# Read until the socket has been quiet for $quiet_ms, or $timeout expires.
sub drain {
	my $out = '';
	my $deadline = time + $timeout;
	my $quiet = $quiet_ms / 1000;
	my $got_anything = 0;

	while (1) {
		my $wait = $got_anything ? $quiet : 1.0;
		if ($sel->can_read($wait)) {
			my $n = sysread($sock, my $buf, 65536);
			if (!defined $n || $n == 0) {
				$out .= "\n[connection closed]\n";
				last;
			}
			$out .= $buf;
			$got_anything = 1;
		} else {
			last if $got_anything;
			last if time >= $deadline;
		}
	}
	return $out;
}
