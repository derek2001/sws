#!/usr/bin/perl -w
use CGI qw(:standard);
use strict;


my $query = new CGI;

my $fname = $query->param('FirstName');
my $lname = $query->param('LastName');

print $query->header("text/plain");
print "This is $fname $lname\n";
open (MYFILE, '>>/home/camino/data.txt'); 
print MYFILE "fname:$fname lname:$lname\n"; 
close(MYFILE);
