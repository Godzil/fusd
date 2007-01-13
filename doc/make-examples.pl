#!/usr/bin/perl -w


foreach $path (@ARGV) {
    $writing = 0;

    if (!open(IN, $path)) {
	print "trying to open $path: $!\n";
	next;
    }

    while ($line = <IN>) {
	if ($line =~ /EXAMPLE (\w*) ([\w\-\.]*)/) {
	    $command = $1;
	    $filename = $2 . ".example";

	    if ($command eq 'START') {
		if ($writing == 0) {
		    if (!open(OUT, ">>$filename")) {
			print "trying to write to $filename: $!\n";
		    } else {
			print "$path: writing to $filename\n";
			$writing = 1;
		    }
		} else {
		    print "$path: got $line while already writing!\n";
		}
	    }

	    if ($command eq 'STOP') {
		if ($writing == 1) {
		    close(OUT);
		    $writing = 0;
		} else {
		    chomp($line);
		    die "$path line $.: got $line when not writing!\n";
		}
	    }
	} else {
	    if ($writing && $line !~ /SKIPLINE/) {
		print OUT $line;
	    }
	}
    }
    if ($writing) {
	close(OUT);
    }
    close(IN);
}

