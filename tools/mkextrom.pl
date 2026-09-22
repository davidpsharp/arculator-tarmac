#!/usr/bin/perl
# mkextrom.pl - build an Acorn extension ROM image from RISC OS modules.
#
# Usage: mkextrom.pl -o roms/arcrom_ext [-s 65536] module1,ffa module2,ffa ...
#
# Layout (matches the arcrom_ext shipped with Arculator 2.2):
#   0x0000  16-byte header: 00 03 00 87 then zeros
#   0x0010  chunk directory, 8 bytes per entry: word (type 0x81 = relocatable
#           module | size << 8), word offset; terminated by a zero entry
#   0x0100  first module, each module padded to a 0x100 boundary
#   end-16  word: ROM size; word: checksum; "ExtnROM0"
# The checksum is the 32-bit sum of every word up to and including the size
# word (i.e. everything except the checksum and the "ExtnROM0" signature).
use strict;
use warnings;
use Getopt::Long;

my $out;
my $size = 65536;
GetOptions('o=s' => \$out, 's=i' => \$size) && defined $out && @ARGV
	or die "usage: mkextrom.pl -o OUTPUT [-s SIZE] MODULE...\n";

my @mods;
for my $f (@ARGV) {
	open my $fh, '<:raw', $f or die "$f: $!\n";
	local $/;
	my $d = <$fh>;
	close $fh;
	die "$f: module size is not a multiple of 4\n" if length($d) % 4;
	push @mods, { name => $f, data => $d };
}

my $rom = "\x00\x03\x00\x87" . ("\x00" x 12);
my $dir = '';
my $off = 0x100;
my $body = '';
for my $m (@mods) {
	my $len = length $m->{data};
	$dir .= pack('VV', 0x81 | ($len << 8), $off);
	$body .= "\x00" x ($off - 0x100 - length $body);
	$body .= $m->{data};
	$off += $len;
	$off = ($off + 0xff) & ~0xff;
	printf "%-40s %6d bytes at 0x%05x\n", $m->{name}, $len, $off - (($len + 0xff) & ~0xff);
}
$dir .= pack('VV', 0, 0);
die "chunk directory overflows the 0x100 header area\n" if length($rom) + length($dir) > 0x100;
$rom .= $dir;
$rom .= "\x00" x (0x100 - length $rom);
$rom .= $body;
die sprintf("modules need %d bytes, ROM is %d\n", length($rom) + 16, $size) if length($rom) + 16 > $size;
$rom .= "\x00" x ($size - 16 - length $rom);
$rom .= pack('V', $size);

my $sum = 0;
$sum = ($sum + $_) & 0xffffffff for unpack('V*', $rom);
$rom .= pack('V', $sum) . 'ExtnROM0';
die "internal: bad length\n" unless length($rom) == $size;

open my $oh, '>:raw', $out or die "$out: $!\n";
print $oh $rom;
close $oh;
printf "%s: %d bytes, checksum %08x\n", $out, $size, $sum;
