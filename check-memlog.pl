#!/usr/bin/perl -W

# netmon
# Compare MALLOC and FREE numbers, in the netmon MEMORY log debug output.
# See source netmon package -> src/util.h DEBUG_DYNMEM constant.
# Sébastien Millet, September 2013

use strict;

  # If true, trye to detect cases where a realloc is done for a given
  # variable, with no malloc before, to adjust malloc/free counting.
my $HACK_REALLOC = 1;

my $count = 0;
my $free = 0;
my $malloc = 0;
my $realloc = 0;
my $unknown = 0;
my %save_realloc;
my %save_malloc;

print("'Line' column:    input file line number\n");
print("'Malloc' column:  number of malloc seen\n");
print("'Realloc' column: number of realloc seen\n");
print("'Free' column:    number of free seen\n");
print("'Delta' column:   difference between malloc and free\n");
print("'Unk' column:     number of unknown seen\n");
printf("Line  \tMalloc\tReallc\tFree  \tDelta \tUnk   \n", $count, $malloc, $realloc, $free, ($malloc - $free), $unknown);

while (my $l = <>) {
  chomp $l;

  if ($l =~ m/\sFREE\.\.\.\s/) {
    $free++;
  } elsif ($l =~ m/\sMALLOC\.\s/) {
    $malloc++;
  } elsif ($l =~ m/\sREALLOC\s/) {
    $realloc++;
  } else {
    $unknown++;
  }

	my ($op, $var, $size);
  if ($HACK_REALLOC) {
    if (($op, $var, $size) = $l =~ m/^[^	]+	([^	]+)	([^	]+)	([^	]+)/) {
#      print("op = '$op', var = '$var', size = $size\n");
      $var = substr($var, 1) if (substr($var, 0, 1) eq '*');
			$save_realloc{$var} = 1 if ($op eq 'REALLOC');
			$save_malloc{$var} = 1 if ($op eq 'MALLOC.');
		}
  }

  $count++;

  printf("L%05d\tM%05d\tR%05d\tF%05d\tD%05d\tU%05d\n", $count, $malloc, $realloc, $free, ($malloc - $free), $unknown);

#  last if ($count >= 10)
}

print(STDERR "$count line(s) read\n");
printf(STDERR "L%05d\tM%05d\tR%05d\tF%05d\tD%05d\tU%05d\n", $count, $malloc, $realloc, $free, ($malloc - $free), $unknown);
my $bilan = $malloc - $free;
print(STDERR "'malloc' - 'free' = $bilan\n");

if ($HACK_REALLOC) {
  my $adjust = 0;
  foreach my $e (keys %save_realloc) {
    if (!exists($save_malloc{$e})) {
#    print("'$e' realloc'ed but never malloc'ed\n");
      $adjust++;
    }
  }
  print(STDERR "'adjust' factor = $adjust\n");
  $bilan += $adjust;
  print(STDERR "'malloc' - 'free' + 'adjust' = $bilan\n");
}

if ($bilan == 0) {
  print(STDERR "\nOK\n\n");
} else {
  print(STDERR "\n!!!!!!!!!! NOT OK !!!!!!!!!!\n\n");
}

