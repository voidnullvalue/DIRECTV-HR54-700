#!/usr/bin/perl
# Offline dictionary check against a crypt(3) hash read from your OWN device.
#
# The target hash is NEVER embedded here. Supply it out of band, because a
# committed hash of a device root password is itself a credential artifact.
#
#   printf '%s\n' "$HASH" | ./crack_root_hash.pl < wordlist.txt
#   ./crack_root_hash.pl --hash-file ~/.hr54-root.hash < wordlist.txt
#   HASH='...' ./crack_root_hash.pl < wordlist.txt
#
# This only tells you whether a candidate matches. It is here so an owner can
# check their own receiver's password strength; see docs/NOT_INCLUDED.md for
# why the original project's hash is not published.
use strict;
use warnings;

my $target;
if (defined $ENV{HASH} && $ENV{HASH} =~ /\S/) {
    $target = $ENV{HASH};
} elsif (@ARGV && $ARGV[0] eq '--hash-file' && @ARGV > 1) {
    local $/;
    open my $fh, '<', $ARGV[1] or die "cannot read $ARGV[1]: $!\n";
    $target = <$fh>;
    close $fh;
} elsif (@ARGV && $ARGV[0] =~ /\S/) {
    $target = shift @ARGV;
} else {
    my $t = do { local $/; <STDIN> };
    # Allow "HASH" or "HASH wordlist" on stdin.
    my @f = split /\s+/, (defined $t ? $t : '');
    $target = $f[0];
    @ARGV = @f[1 .. $#f];
}

$target = '' unless defined $target;
$target =~ s/\s+\z//;
die "usage: $0 [--hash-file FILE] | HASH=...  < wordlist\n"
    unless $target =~ /^\$[0-9a-z]+\$/;

while (my $candidate = <STDIN>) {
    chomp $candidate;
    next if $candidate eq '';
    if (crypt($candidate, $target) eq $target) {
        print "$candidate\n";
        exit 0;
    }
}
exit 1;
