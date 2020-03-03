#!/usr/bin/perl -w
#
# Simple test suite for transcode
# Written by Andrew Church <achurch@achurch.org>
#
# This file is part of transcode, a video stream processing tool.
# transcode is free software, distributable under the terms of the GNU
# General Public License (version 2 or later).  See the file COPYING
# for details.

use POSIX;  # for floor() and ceil()

use constant CSP_YUV => 1;
use constant CSP_RGB => 2;
use constant WIDTH => 704;
use constant HEIGHT => 576;
use constant NFRAMES => 3;

my $IN_AVI = "in.avi";
my $OUT_AVI = "out.avi";
my $STDOUT_LOG = "stdout.log";
my $STDERR_LOG = "stderr.log";
my $TMPDIR = undef;

my @Tests = ();       # array of test names in order to run
my %Tests = ();       # data for tests (key is test name)

my $Verbose = 0;      # let transcode write to stdout/stderr instead of logs
my $KeepTemp = 0;     # keep temporary directory/files around?
my $ListTests = 0;    # list available tests instead of running them?
my $AccelMode = "";   # acceleration flags for transcode
my %TestsToRun = ();  # user-specified list of tests to run
my @TestsToSkip = (); # user-specified list of tests to assume passed
my %VideoData;        # for saving raw output data to compare against

########
# Initialization
&init();

########
# First make sure that -x raw -y raw works, and get an AVI in transcode's
# output format for comparison purposes
&add_test("raw/raw (YUV)", [],
          "Test input/output using raw YUV420P data",
          \&test_raw_raw, CSP_YUV);
&add_test("raw/raw (RGB)", [],
          "Test input/output using raw RGB data",
          \&test_raw_raw, CSP_RGB);
# Alias for both of the above
&add_test("raw", ["raw/raw (YUV)", "raw/raw (RGB)"],
          "Test input/output using raw data (all colorspaces)");

# Test colorspace conversion
&add_test("raw/raw (YUV->RGB)", ["raw"],
          "Test colorspace conversion from YUV420P to RGB",
          \&test_raw_raw_csp, CSP_YUV, CSP_RGB);
&add_test("raw/raw (RGB->YUV)", ["raw"],
          "Test colorspace conversion from RGB to YUV420P",
          \&test_raw_raw_csp, CSP_RGB, CSP_YUV);
&add_test("raw-csp", ["raw", "raw/raw (YUV->RGB)", "raw/raw (RGB->YUV)"],
          "Test colorspace conversion");

# Test import_ffmpeg on test video
&add_test("-x ffmpeg (raw)", ["raw"],
          "Test input of raw data using import_ffmpeg",
          \&test_import, "ffmpeg", \$VideoData{&CSP_YUV}, CSP_YUV);

# Test various export modules by running their output through ffmpeg
&add_test("-y ffmpeg", ["-x ffmpeg (raw)"],
          "Test output using export_ffmpeg",
          \&test_export_x_ffmpeg, "ffmpeg", CSP_YUV, "mpeg4");
&add_test("-y xvid4", ["-x ffmpeg (raw)"],
          "Test output using export_xvid4",
          \&test_export_x_ffmpeg, "xvid4", CSP_YUV);

#X# # Test the PPM export module
#X# &add_test("-y ppm", ["raw"],
#X#           "Test output using export_ppm",
#X#           \&test_export_ppm);

# Test the core video operations

@csplist = ();
@vidcore_tests = ("-j", "-I", "-X", "-B", "-Z", "-Y", "-r", "-z",
                  "-l", "-k", "-K", "-G", "-C");

foreach $cspset ([CSP_YUV,"yuv","YUV420P"], [CSP_RGB,"rgb","RGB24"]) {

    my ($cspid,$csp,$cspname) = @$cspset;
    push @csplist, $csp;

    &add_test("-j N:$csp", ["raw"],
              "Test -j with one parameter ($cspname)",
              \&test_vidcore, $cspid, ["-j", "10",
                               \&vidcore_crop, 10, 0, 10, 0]);
    &add_test("-j N,N:$csp", ["raw"],
              "Test -j with two parameters ($cspname)",
              \&test_vidcore, $cspid, ["-j", "10,20",
                               \&vidcore_crop, 10, 20, 10, 20]);
    &add_test("-j N,N,N:$csp", ["raw"],
              "Test -j with three parameters ($cspname)",
              \&test_vidcore, $cspid, ["-j", "10,20,30",
                               \&vidcore_crop, 10, 20, 30, 20]);
    &add_test("-j N,N,N,N:$csp", ["raw"],
              "Test -j with four parameters ($cspname)",
              \&test_vidcore, $cspid, ["-j", "10,20,30,40",
                               \&vidcore_crop, 10, 20, 30, 40]);
    &add_test("-j -N,-N,-N,-N:$csp", ["raw"],
              "Test -j with padding ($cspname)",
              \&test_vidcore, $cspid, ["-j", "-10,-20,-30,-40",
                               \&vidcore_crop, -10, -20, -30, -40]);
    &add_test("-j N,-N,N,-N:$csp", ["raw"],
              "Test -j with V crop / H pad ($cspname)",
              \&test_vidcore, $cspid, ["-j", "10,-20,30,-40",
                               \&vidcore_crop, 10, -20, 30, -40]);
    &add_test("-j -N,N,-N,N:$csp", ["raw"],
              "Test -j with V pad / H crop ($cspname)",
              \&test_vidcore, $cspid, ["-j", "-10,20,-30,40",
                               \&vidcore_crop, -10, 20, -30, 40]);
    &add_test("-j:$csp",
              ["-j N:$csp", "-j N,N:$csp", "-j N,N,N:$csp", "-j N,N,N,N:$csp",
               "-j -N,-N,-N,-N:$csp", "-j N,-N,N,-N:$csp",
               "-j -N,N,-N,N:$csp"],
              "Test -j ($cspname)");

    &add_test("-I 1:$csp", ["raw"],
              "Test -I in interpolation mode ($cspname)",
              \&test_vidcore, $cspid, ["-I", "1",
                               \&vidcore_deint_interpolate]);
    &add_test("-I 3:$csp", ["raw"],
              "Test -I in drop-field-and-zoom mode ($cspname)",
              \&test_vidcore, $cspid, ["-I", "3",
                               \&vidcore_deint_dropzoom]);
    &add_test("-I 4:$csp", ["raw"],
              "Test -I in drop-field mode ($cspname)",
              \&test_vidcore, $cspid, ["-I", "4",
                               \&vidcore_deint_dropfield]);
    &add_test("-I 5:$csp", ["raw"],
              "Test -I in linear-blend mode ($cspname)",
              \&test_vidcore, $cspid, ["-I", "5",
                               \&vidcore_deint_linear_blend]);
    &add_test("-I:$csp", ["-I 1:$csp", "-I 3:$csp", "-I 4:$csp", "-I 5:$csp"],
              "Test -I ($cspname)");

    # Be careful with values here!  Truncation by accelerated rescale()
    # during vertical resize can cause false errors (cases where the byte
    # value is off by 1 because rounding went the other way).  -X 6 seems
    # to be safe.
    &add_test("-X y:$csp", ["raw"],
              "Test -X with height only ($cspname)",
              \&test_vidcore, $cspid, ["-X", "6",
                               \&vidcore_resize, 6*32, 0]);
    &add_test("-X 0,x:$csp", ["raw"],
              "Test -X with width only ($cspname)",
              \&test_vidcore, $cspid, ["-X", "0,11",
                               \&vidcore_resize, 0, 11*32]);
    &add_test("-X y,x:$csp", ["raw"],
              "Test -X with width and height ($cspname)",
              \&test_vidcore, $cspid, ["-X", "6,11",
                               \&vidcore_resize, 6*32, 11*32]);
    &add_test("-X y,x,M:$csp", ["raw"],
              "Test -X with width, height, and multiplier ($cspname)",
              \&test_vidcore, $cspid, ["-X", "24,44,8",
                               \&vidcore_resize, 24*8, 44*8]);
    &add_test("-X:$csp",
              ["-X y:$csp", "-X 0,x:$csp", "-X y,x:$csp", "-X y,x,M:$csp"],
              "Test -X ($cspname)");

    &add_test("-B y:$csp", ["raw"],
              "Test -B with height only ($cspname)",
              \&test_vidcore, $cspid, ["-B", "6",
                               \&vidcore_resize, -6*32, 0]);
    &add_test("-B 0,x:$csp", ["raw"],
              "Test -B with width only ($cspname)",
              \&test_vidcore, $cspid, ["-B", "0,11",
                               \&vidcore_resize, 0, -11*32]);
    &add_test("-B y,x:$csp", ["raw"],
              "Test -B with width and height ($cspname)",
              \&test_vidcore, $cspid, ["-B", "6,11",
                               \&vidcore_resize, -6*32, -11*32]);
    &add_test("-B y,x,M:$csp", ["raw"],
              "Test -B with width, height, and multiplier ($cspname)",
              \&test_vidcore, $cspid, ["-B", "24,44,8",
                               \&vidcore_resize, -24*8, -44*8]);
    &add_test("-B:$csp",
              ["-B y:$csp", "-B 0,x:$csp", "-B y,x:$csp", "-B y,x,M:$csp"],
              "Test -B ($cspname)");

    &add_test("-Z WxH,fast:$csp", ["raw"],
              "Test -Z (fast mode) ($cspname)",
              \&test_vidcore, $cspid, ["-Z",
                               ((WIDTH-11*32)."x".(HEIGHT+6*32).",fast"),
                               \&vidcore_resize, 6*32, -11*32]);
    &add_test("-Z WxH:$csp", ["raw"],
              "Test -Z (slow mode) ($cspname)",
              \&test_vidcore, $cspid, ["-Z", ((WIDTH-76)."x".(HEIGHT+76)),
                               \&vidcore_zoom, WIDTH-76, HEIGHT+76]);
    &add_test("-Z:$csp", ["-Z WxH,fast:$csp", "-Z WxH:$csp"],
              "Test -Z ($cspname)");

    &add_test("-Y N:$csp", ["raw"],
              "Test -Y with one parameter ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "10",
                               \&vidcore_crop, 10, 0, 10, 0]);
    &add_test("-Y N,N:$csp", ["raw"],
              "Test -Y with two parameters ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "10,20",
                               \&vidcore_crop, 10, 20, 10, 20]);
    &add_test("-Y N,N,N:$csp", ["raw"],
              "Test -Y with three parameters ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "10,20,30",
                               \&vidcore_crop, 10, 20, 30, 20]);
    &add_test("-Y N,N,N,N:$csp", ["raw"],
              "Test -Y with four parameters ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "10,20,30,40",
                               \&vidcore_crop, 10, 20, 30, 40]);
    &add_test("-Y -N,-N,-N,-N:$csp", ["raw"],
              "Test -Y with padding ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "-10,-20,-30,-40",
                               \&vidcore_crop, -10, -20, -30, -40]);
    &add_test("-Y N,-N,N,-N:$csp", ["raw"],
              "Test -Y with V crop / H pad ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "10,-20,30,-40",
                               \&vidcore_crop, 10, -20, 30, -40]);
    &add_test("-Y -N,N,-N,N:$csp", ["raw"],
              "Test -Y with V pad / H crop ($cspname)",
              \&test_vidcore, $cspid, ["-Y", "-10,20,-30,40",
                               \&vidcore_crop, -10, 20, -30, 40]);
    &add_test("-Y:$csp",
              ["-Y N:$csp", "-Y N,N:$csp", "-Y N,N,N:$csp", "-Y N,N,N,N:$csp",
               "-Y -N,-N,-N,-N:$csp", "-Y N,-N,N,-N:$csp",
               "-Y -N,N,-N,N:$csp"],
              "Test -Y ($cspname)");

    &add_test("-r n:$csp", ["raw"],
              "Test -r with one parameter ($cspname)",
              \&test_vidcore, $cspid, ["-r", "2", \&vidcore_reduce, 2, 2]);
    &add_test("-r y,x:$csp", ["raw"],
              "Test -r with two parameters ($cspname)",
              \&test_vidcore, $cspid, ["-r", "2,5", \&vidcore_reduce, 2, 5]);
    &add_test("-r y,1:$csp", ["raw"],
              "Test -r with width reduction == 1 ($cspname)",
              \&test_vidcore, $cspid, ["-r", "2,1", \&vidcore_reduce, 2, 1]);
    &add_test("-r 1,x:$csp", ["raw"],
              "Test -r with height reduction == 1 ($cspname)",
              \&test_vidcore, $cspid, ["-r", "1,5", \&vidcore_reduce, 1, 5]);
    &add_test("-r 1,1:$csp", ["raw"],
              "Test -r with width/height reduction == 1 (no-op) ($cspname)",
              \&test_vidcore, $cspid, ["-r", "1,1"]);
    &add_test("-r:$csp", ["-r n:$csp", "-r y,x:$csp", "-r y,1:$csp",
                     "-r 1,x:$csp", "-r 1,1:$csp"],
              "Test -r ($cspname)");

    &add_test("-z:$csp", ["raw"],
              "Test -z ($cspname)",
              \&test_vidcore, $cspid, ["-z", undef, \&vidcore_flip_v]),
    &add_test("-l:$csp", ["raw"],
              "Test -l ($cspname)",
              \&test_vidcore, $cspid, ["-l", undef, \&vidcore_flip_h]),
    &add_test("-k:$csp", ["raw"],
              "Test -k ($cspname)",
              \&test_vidcore, $cspid, ["-k", undef, \&vidcore_rgbswap]),
    &add_test("-K:$csp", ["raw"],
              "Test -K ($cspname)",
              \&test_vidcore, $cspid, ["-K", undef, \&vidcore_grayscale]),
    &add_test("-G:$csp", ["raw"],
              "Test -G ($cspname)",
              \&test_vidcore, $cspid, ["-G", "1.2",
                               \&vidcore_gamma_adjust, 1.2]),
    &add_test("-C:$csp", ["raw"],
              "Test -C ($cspname)",
              \&test_vidcore, $cspid, ["-C", "3",
                               \&vidcore_antialias, 1/3, 1/2],
                              ["--antialias_para", ((1/3).",".(1/2))]),

    &add_test("vidcore:$csp", [map {"$_:$csp"} @vidcore_tests],
              "Test all video core operations ($cspname)");

}  # for each colorspace

foreach $test (@vidcore_tests) {
    &add_test($test, [map {"$test:$_"} @csplist],
              "Test $test (all colorspaces)");
}

&add_test("vidcore", [map {"vidcore:$_"} @csplist],
          "Test all video core operations (all colorspaces)");

########
# Run all (or specified) tests
my $exitcode = 0;
if (!$ListTests) {
    foreach $test (@TestsToSkip) {
        $Tests{$test}{'run'} = 1;
        $Tests{$test}{'succeeded'} = 1;
    }
    foreach $test (@Tests) {
        $exitcode ||= !&run_test($test) if !%TestsToRun || $TestsToRun{$test};
    }
}

########
# Finished, clean up
&cleanup();
exit $exitcode;

###########################################################################
###########################################################################

# Initialization

sub init
{
    for (my $i = 0; $i < @ARGV; $i++) {
        if ($ARGV[$i] =~ /^(--(.*)|-(.)(.*))/) {
            my $option = $2 ? "--$2" : "-$3";
            my $optval = $4;
            if ($option eq "-h" || $option eq "--help") {
                print STDERR "Usage: $0 [-kvl] [-a accel] [-t test [-t test...]]\n";
                print STDERR "     -k: don't delete temporary directory\n";
		print STDERR "     -v: verbose (transcode output to stdout/stderr\n";
                print STDERR "     -l: list tests available\n";
                print STDERR "     -a: specify acceleration types (transcode --accel)\n";
                print STDERR "     -t: specify test(s) to run\n";
                print STDERR "     -T: specify test(s) to assume passed\n";
                exit 0;
            } elsif ($option eq "-k") {
                $KeepTemp = 1;
	    } elsif ($option eq "-v") {
		$Verbose = 1;
            } elsif ($option eq "-l") {
                $ListTests = 1;
            } elsif ($option eq "-a") {
                $optval = $ARGV[++$i] if $optval eq "";
                $AccelMode = $optval;
            } elsif ($option eq "-t") {
                $optval = $ARGV[++$i] if $optval eq "";
                $TestsToRun{$optval} = 1;
            } elsif ($option eq "-T") {
                $optval = $ARGV[++$i] if $optval eq "";
                push @TestsToSkip, $optval;
            } else {
                &fatal("Invalid option `$option' ($0 -h for help)");
            }
        }
    }

    $TMPDIR = $ENV{'TMPDIR'} || "/tmp";
    $TMPDIR .= "/tctest.$$";
    mkdir $TMPDIR, 0700 or &fatal("mkdir($TMPDIR): $!");
    print STDERR "Using temporary directory $TMPDIR\n";
}

###########################################################################

# Cleanup

sub cleanup
{
    if (!$KeepTemp && $TMPDIR && -d $TMPDIR) {
	foreach $file ("$IN_AVI","$OUT_AVI","$STDOUT_LOG","$STDERR_LOG") {
	    if (-f "$TMPDIR/$file") {
		unlink "$TMPDIR/$file"
		    or print STDERR "unlink($TMPDIR/$file): $!\n";
	    }
	}
        rmdir $TMPDIR or print STDERR "rmdir($TMPDIR): $!\n";
    }
}

###########################################################################

# Fatal error (plus cleanup)--use this instead of die

sub fatal
{
    print STDERR "$_[0]\n";
    &cleanup();
    exit(-1);
}

###########################################################################

# Run transcode with the given input file and arguments, and return the
# contents of the output file.  Returns undef if transcode exits with an
# error or the output file does not exist.

sub transcode
{
    my ($indata,@args) = @_;
    my $outdata;
    local *F;
    local $/ = undef;

    if ($AccelMode) {
        push @args, "--accel", $AccelMode;
    }
    push @args, "--zoom_filter", "triangle";
    open F, ">$TMPDIR/$IN_AVI" or &fatal("create $TMPDIR/$IN_AVI: $!");
    syswrite(F, $indata) == length($indata) or &fatal("write $TMPDIR/$IN_AVI: $!");
    close F;
    my $pid = fork();
    &fatal("fork(): $!") if !defined($pid);
    if (!$pid) {
        open STDIN, "</dev/null";
        open STDOUT, ">$TMPDIR/$STDOUT_LOG" if !$Verbose;
        open STDERR, ">$TMPDIR/$STDERR_LOG" if !$Verbose;
	@args = ("transcode", "-i", "$TMPDIR/$IN_AVI",
                              "-o", "$TMPDIR/$OUT_AVI", @args);
	print join(" ",@args)."\n" if $Verbose;
	exec @args or die;
    }
    &fatal("waitpid($pid): $!") if !waitpid($pid,0);
    return undef if $? != 0;
    if (open(F, "<$TMPDIR/$OUT_AVI")) {
	$outdata = <F>;
	close F;
	return $outdata;
    }
    my @files = glob("$TMPDIR/$OUT_AVI*");
    if (@files) {
	my $outdata = "";
	foreach $file (@files) {
	    if (!open(F, "<$file")) {
		print STDERR "$file: $!\n";
		return undef;
	    }
	    $outdata .= <F>;
	    close F;
	    unlink $file if !$KeepTemp;
	}
    }
    return undef;
}

###########################################################################

# Add a test to the global list of tests.  Pass the test name in $name, the
# the list of dependent tests (array reference) in $deps, a description of
# the test in $desc, the function to call in $func, and any parameters to
# the function in @args.  The function should return undef for success or
# an error message (string) for failure.
#
# If $func and @args are omitted, the test becomes an "alias" for all tests
# it depends on; when run, its dependencies are executed but nothing is
# done for the test itself.

sub add_test
{
    my ($name, $deps, $desc, $func, @args) = @_;
    $Tests{$name} = {deps => $deps, func => $func, args => [@args], run => 0};
    push @Tests, $name;
    printf "%20s | %s\n", $name, $desc if $ListTests;
}

###########################################################################

# Run a transcode test (including any dependencies) and print the result.
# Pass the test name in $name.  Returns 1 if the test succeeded, 0 if it
# (or any dependencies) failed.

sub run_test
{
    my ($name) = @_;
    my $result;
    local $| = 1;

    if (!$Tests{$name}) {
        print "$name... NOT FOUND (bug in script?)\n";
        $Tests{$name}{'run'} = 1;
        $Tests{$name}{'succeeded'} = 0;
        return 0;
    }
    if (!$Tests{$name}{'run'}) {
        if ($Tests{$name}{'recurse'}) {
            $result = "dependency loop in test script";
        } else {
            $Tests{$name}{'recurse'}++;
            foreach $dep (@{$Tests{$name}{'deps'}}) {
                my $res2 = &run_test($dep);
                if (!$res2) {
                    $result = "dependency `$dep' failed" if !defined($result);
                }
            }
            $Tests{$name}{'recurse'}--;
        }
        my $func = $Tests{$name}{'func'};
        my $args = $Tests{$name}{'args'};
        if ($func) {
            print "$name... ";
            if (!defined($result)) {
                $result = &$func(@$args);
            }
        }
        $Tests{$name}{'run'} = 1;
        if (defined($result)) {
            print "FAILED ($result)\n" if $func;
            $Tests{$name}{'succeeded'} = 0;
        } else {
            print "ok\n" if $func;
            $Tests{$name}{'succeeded'} = 1;
        }
    }
    return $Tests{$name}{'succeeded'};
}

###########################################################################
###########################################################################

# Various tests.

###########################################################################

# Test "-x raw,null -y raw,null", to ensure raw input and output works.
# Pass a CSP_* constant in $csp.  The output frame will be stored in
# $VideoData{$csp}.

sub test_raw_raw
{
    my ($csp) = @_;

    my $raw_in = &gen_raw_avi(WIDTH, HEIGHT, NFRAMES, $csp);
    my $i = index($raw_in, "movi00db");  # find first frame
    &fatal("***BUG*** can't find frame in test input") if $i < 0;
    my $raw_frame = substr($raw_in, $i+4,
                           8+unpack("V",substr($raw_in,$i+8,4)));
    my @colorspace_args = ();
    push @colorspace_args, "-V", "rgb24" if $csp == CSP_RGB;
    $VideoData{$csp} = &transcode($raw_in, "-x", "raw,null", "-y",
                                   "raw,null", @colorspace_args);
    return "transcode failed" if !$VideoData{$csp};
    $i = index($VideoData{$csp}, "movi00db");
    return "can't find any frames in output file" if $i < 0;
    return "bad output data"
        if (substr($VideoData{$csp}, $i+4, length($raw_frame)*NFRAMES)
            ne $raw_frame x NFRAMES);
    return undef;
}

###########################################################################

# Test raw input/output with colorspace conversion.

sub test_raw_raw_csp
{
    my ($csp_in, $csp_out) = @_;

    my $data = $VideoData{$csp_in};
    &fatal("***BUG*** missing input data for CSP $csp_in") if !$data;
    &fatal("***BUG*** missing output data for CSP $csp_out")
        if !$VideoData{$csp_out};
    my $outcsp_arg = $csp_out==CSP_RGB ? "rgb" : "i420";
    my @colorspace_args = ();
    push @colorspace_args, "-V", "rgb24" if $csp_in == CSP_RGB;
    $data = &transcode($data, "-x", "raw,null", "-y", "raw,null",
                       "-F", $outcsp_arg, @colorspace_args);
    return "transcode failed" if !$data;
    return "bad output data" if $data ne $VideoData{$csp_out};
    return undef;
}

###########################################################################

# Test a generic video import module.  Pass the module name (with any
# parameters) in $vimport_mod, and a CSP_* colorspace constant in $csp
# (this is the target colorspace to be used by transcode).

sub test_import
{
    my ($vimport_mod, $dataref, $csp) = @_;

    &fatal("***BUG*** missing output data for CSP $csp")
        if !$VideoData{$csp};
    my @colorspace_args = ();
    push @colorspace_args, "-V", "rgb24" if $csp == CSP_RGB;
    $data = &transcode($$dataref, "-x", "$vimport_mod,null", "-y", "raw,null",
                       @colorspace_args);
    return "transcode failed" if !$data;
    return "bad output data" if $data ne $VideoData{$csp};
    return undef;
}

###########################################################################

# Test a generic video export module, by running the output back through
# -x ffmpeg.  Pass the module name (with any parameters) in $vexport_mod, a
# CSP_* colorspace constant in $csp,

sub test_export_x_ffmpeg
{
    my ($vexport_mod, $csp, $F_arg) = @_;

    my $csp_data = $VideoData{$csp};
    &fatal("***BUG*** missing input data for CSP $csp") if !$csp_data;
    my @extra_args = ();
    push @extra_args, "-F", $F_arg if defined($F_arg);
    my @colorspace_args = ();
    push @colorspace_args, "-V", "rgb24" if $csp == CSP_RGB;
    my $export_data = &transcode($csp_data, "-x", "raw,null", "-y",
                                 "$vexport_mod,null", @extra_args,
                                 @colorspace_args);
    return "transcode (export) failed" if !$export_data;
    my $import_data = &transcode($export_data, "-x", "ffmpeg,null", "-y",
                                 "raw,null", @colorspace_args);
    return "transcode (import) failed" if !$import_data;
    return "bad data" if $import_data ne $VideoData{$csp};
    return undef;
}

###########################################################################

# Test one or more video core operations.  Each parameter is a reference to
# an array containing, in the following order:
#     * The transcode command-line option, e.g. "-j"
#     * The parameter to the option, or undef if there is no parameter
#     * The function (vidcore_* below) that implements the transformation
#          (can be omitted if no transformation should be executed)
#     * Parameters to the function, if any
# The operations are assumed to be performed by transcode in the order they
# are passed to this function.

sub test_vidcore
{
    my $colorspace = shift @_;
    my @cmdline = ("-x", "raw,null", "-y", "raw,null");
    push @cmdline, "-V", "rgb24" if $colorspace == CSP_RGB;
    my $bpp = $colorspace==CSP_RGB ? 24 : 12;  # total bits per pixel

    # Diagonalize the test frame, to try and catch more bugs
    if (!$vidcore_in_frame{$colorspace}) {
        my $frame = $colorspace==CSP_RGB ? &gen_rgb_frame(WIDTH,HEIGHT)
                                         : &gen_yuv_frame(WIDTH,HEIGHT);
        my $Bpp = $colorspace==CSP_RGB ? 3 : 1;
        my $Bp2 = $Bpp*2;
        for (my $y = 2; $y < HEIGHT; $y += 2) {
            $frame = substr($frame, 0, $y*WIDTH*$Bpp)
                   . substr($frame, $y*WIDTH*$Bpp-$Bp2, $Bp2)
                   . substr($frame, $y*WIDTH*$Bpp,
                            (HEIGHT-$y)*WIDTH*$Bpp-$Bp2)
                   . substr($frame, HEIGHT*WIDTH*$Bpp);
        }
        if ($colorspace == CSP_YUV) {
            my $ofs = HEIGHT*WIDTH*$Bpp;
            my $w2 = int(WIDTH/2);
            my $h2 = int(HEIGHT/2);
            for (my $y = 1; $y < $h2; $y++) {
                $frame = substr($frame, 0, $ofs+$y*$w2)
                       . substr($frame, $ofs+$y*$w2-1, 1)
                       . substr($frame, $ofs+$y*$w2, ($h2-$y)*$w2-1)
                       . substr($frame, $ofs+$h2*$w2);
            }
            $ofs += $h2*$w2*$Bpp;
            for (my $y = 1; $y < $h2; $y++) {
                $frame = substr($frame, 0, $ofs+$y*$w2)
                       . substr($frame, $ofs+$y*$w2-1, 1)
                       . substr($frame, $ofs+$y*$w2, ($h2-$y)*$w2-1);
            }
        }
        $vidcore_in_frame{$colorspace} = $frame;
    }

    # Generate command line and expected output frame
    my $out_frame = $vidcore_in_frame{$colorspace};
    my $out_w = WIDTH;
    my $out_h = HEIGHT;
    foreach $op (@_) {
        push @cmdline, $$op[0];
        push @cmdline, $$op[1] if defined($$op[1]);
        if ($$op[2]) {
            if (!&{$$op[2]}($colorspace, \$out_frame, \$out_w, \$out_h,
                            @$op[3..$#$op])) {
                return "Video operation for $$op[0] not implemented";
            }
            if (length($out_frame) != $out_w * $out_h * $bpp / 8) {
                return "Video operation for $$op[0] gave wrong size result"
                     . " (tester bug)";
            }
        }
    }

    # Run transcode
    my $in_avi = &gen_raw_avi(WIDTH, HEIGHT, NFRAMES, $colorspace,
                              $vidcore_in_frame{$colorspace});
    my $out_avi = &transcode($in_avi, @cmdline);

    # Check output data
    my $pos = index($out_avi, "movi00db");
    $pos += 4 if $pos >= 0;
    for (my $i = 0; $i < NFRAMES; $i++) {
        if ($pos < 0) {
            return "Can't find video data in transcode output";
        }
        my $len = unpack("V", substr($out_avi, $pos+4, 4));
        if ($len != length($out_frame)) {
            return "Video frame has bad size ($len, expected "
                . length($out_frame) . ")";
        }
        if (substr($out_avi, $pos+8, $len) ne $out_frame) {
#open F,">/tmp/t";print F $out_frame;close F;open F,">/tmp/u";print F substr($out_avi,$pos+8,$len);close F;
            return "Incorrect data in video frame";
        }
        $pos = index($out_avi, "00db", $pos+8+$len);
    }
    return undef;
}

################

# -j/-Y
sub vidcore_crop
{
    my ($csp, $frameref, $widthref, $heightref,
        $top, $left, $bottom, $right) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $black = $csp==-2 ? "\x80" : "\0";

    if ($csp == CSP_YUV) {
        my $w2 = int($$widthref/2);
        my $h2 = int($$heightref/2);
        my $Y = substr($$frameref, 0, $$widthref*$$heightref);
        my $U = substr($$frameref, $$widthref*$$heightref, $w2*$h2);
        my $V = substr($$frameref, $$widthref*$$heightref + $w2*$h2, $w2*$h2);
        return 0 if !&vidcore_crop(-1, \$Y, $widthref, $heightref,
                                   $top, $left, $bottom, $right);
        my $wdummy = $w2;
        my $hdummy = $h2;
        return 0 if !&vidcore_crop(-2, \$U, \$wdummy, \$hdummy,
                                   int($top/2), int($left/2),
                                   int($bottom/2), int($right/2));
        return 0 if !&vidcore_crop(-2, \$V, \$w2, \$h2,
                                   int($top/2), int($left/2),
                                   int($bottom/2), int($right/2));
        $$frameref = $Y . $U . $V;
        return 1;
    }

    if ($top > 0) {
        $$frameref = substr($$frameref, $top*$$widthref*$Bpp);
    } elsif ($top < 0) {
        $$frameref = ($black x (-$top*$$widthref*$Bpp)) . $$frameref;
    }
    $$heightref -= $top;
    if ($bottom > 0) {
        $$frameref = substr($$frameref, 0,
                            ($$heightref-$bottom)*$$widthref*$Bpp);
    } elsif ($bottom < 0) {
        $$frameref .= $black x (-$bottom*$$widthref*$Bpp);
    }
    $$heightref -= $bottom;
    my $newframe = "";
    for (my $y = 0; $y < $$heightref; $y++) {
        my $row = substr($$frameref, $y*$$widthref*$Bpp, $$widthref*$Bpp);
        if ($left > 0) {
            $row = substr($row, $left*$Bpp);
        } elsif ($left < 0) {
            $row = ($black x (-$left*$Bpp)) . $row;
        }
        if ($right > 0) {
            $row = substr($row, 0, length($row) - $right*$Bpp);
        } elsif ($right < 0) {
            $row .= $black x (-$right*$Bpp);
        }
        $newframe .= $row;
    }
    $$widthref -= $left + $right;
    $$frameref = $newframe;
    return 1;
}

################

# -I 1
sub vidcore_deint_interpolate
{
    my ($csp, $frameref, $widthref, $heightref) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $Bpl = $$widthref * $Bpp;

    my $newframe = "";
    for (my $y = 0; $y < $$heightref; $y++) {
        if ($y%2 == 0) {
            $newframe .= substr($$frameref, $y*$Bpl, $Bpl);
        } elsif ($y == $$heightref-1) {
            $newframe .= substr($$frameref, ($y-1)*$Bpl, $Bpl);
        } else {
            for (my $x = 0; $x < $Bpl; $x++) {
                my $c1 = ord(substr($$frameref, ($y-1)*$Bpl+$x, 1));
                my $c2 = ord(substr($$frameref, ($y+1)*$Bpl+$x, 1));
                $newframe .= chr(int(($c1+$c2+1)/2));
            }
        }
    }
    $newframe .= substr($$frameref, $$heightref*$Bpl);
    $$frameref = $newframe;
    return 1;
}


# -I 3
sub vidcore_deint_dropzoom
{
    my ($csp, $frameref, $widthref, $heightref) = @_;

    my $oldheight = $$heightref;
    &vidcore_deint_dropfield($csp, $frameref, $widthref, $heightref);
    return &vidcore_zoom($csp, $frameref, $widthref, $heightref, $$widthref,
                         $oldheight);
}


# -I 4
sub vidcore_deint_dropfield
{
    my ($csp, $frameref, $widthref, $heightref) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $Bpl = $$widthref*$Bpp;

    my $newframe = "";
    for (my $y = 0; $y < int($$heightref/2); $y++) {
        $newframe .= substr($$frameref, ($y*2)*$Bpl, $Bpl);
    }
    if ($csp == CSP_YUV) {
        my $ofs = $$widthref * $$heightref;
        $Bpl = int($Bpl/2);
        for (my $y = 0; $y < int($$heightref/2/2); $y++) {
            $newframe .= substr($$frameref, $ofs + ($y*2)*$Bpl, $Bpl);
        }
        $ofs += int($$widthref/2) * int($$heightref/2);
        for (my $y = 0; $y < int($$heightref/2/2); $y++) {
            $newframe .= substr($$frameref, $ofs + ($y*2)*$Bpl, $Bpl);
        }
    }
    $$frameref = $newframe;
    $$heightref = int($$heightref/2);
    return 1;
}


# -I 5
sub vidcore_deint_linear_blend
{
    my ($csp, $frameref, $widthref, $heightref) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $Bpl = $$widthref * $Bpp;

    my $evenframe = "";
    for (my $y = 0; $y < $$heightref; $y++) {
        if ($y%2 == 0) {
            $evenframe .= substr($$frameref, $y*$Bpl, $Bpl);
        } elsif ($y == $$heightref-1) {
            $evenframe .= substr($$frameref, ($y-1)*$Bpl, $Bpl);
        } else {
            for (my $x = 0; $x < $Bpl; $x++) {
                my $c1 = ord(substr($$frameref, ($y-1)*$Bpl+$x, 1));
                my $c2 = ord(substr($$frameref, ($y+1)*$Bpl+$x, 1));
                $evenframe .= chr(int(($c1+$c2+1)/2));
            }
        }
    }

    my $oddframe = "";
    for (my $y = 0; $y < $$heightref; $y++) {
        if ($y%2 == 1) {
            $oddframe .= substr($$frameref, $y*$Bpl, $Bpl);
        } elsif ($y == 0) {
            $oddframe .= substr($$frameref, ($y+1)*$Bpl, $Bpl);
        } elsif ($y == $$heightref-1) {
            $oddframe .= substr($$frameref, ($y-1)*$Bpl, $Bpl);
        } else {
            for (my $x = 0; $x < $Bpl; $x++) {
                my $c1 = ord(substr($$frameref, ($y-1)*$Bpl+$x, 1));
                my $c2 = ord(substr($$frameref, ($y+1)*$Bpl+$x, 1));
                $oddframe .= chr(int(($c1+$c2+1)/2));
            }
        }
    }

    my $newframe = "";
    for (my $i = 0; $i < $$heightref*$Bpl; $i++) {
        my $c1 = ord(substr($evenframe, $i, 1));
        my $c2 = ord(substr($oddframe, $i, 1));
        $newframe .= chr(int(($c1+$c2+1)/2));
    }
    $newframe .= substr($$frameref, $$heightref*$Bpl);

    $$frameref = $newframe;
    return 1;
}

################

# -B/-X
sub vidcore_resize
{
    my ($csp, $frameref, $widthref, $heightref, $resize_h, $resize_w) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $mult = $csp==-2 ? 4 : 8;

    if ($csp == CSP_YUV) {
        my $w2 = int($$widthref/2);
        my $h2 = int($$heightref/2);
        my $Y = substr($$frameref, 0, $$widthref*$$heightref);
        my $U = substr($$frameref, $$widthref*$$heightref, $w2*$h2);
        my $V = substr($$frameref, $$widthref*$$heightref + $w2*$h2, $w2*$h2);
        return 0 if !&vidcore_resize(-1, \$Y, $widthref, $heightref,
                                     $resize_h, $resize_w);
        my $wdummy = $w2;
        my $hdummy = $h2;
        return 0 if !&vidcore_resize(-2, \$U, \$wdummy, \$hdummy,
                                     int($resize_h/2), int($resize_w/2));
        return 0 if !&vidcore_resize(-2, \$V, \$w2, \$h2,
                                     int($resize_h/2), int($resize_w/2));
        $$frameref = $Y . $U . $V;
        return 1;
    }

    my $newframe = $$frameref;

    if ($resize_h) {
        my $Bpl = $$widthref*$Bpp;
        my $new_h = $$heightref + $resize_h;
        my $ratio = $$heightref / $new_h;
        my $oldy_block = int($$heightref/$mult);
        my $y_block = int($new_h/$mult);
        my (@source, @weight1, @weight2);
        for (my $i = 0; $i < $y_block; $i++) {
            my $oldi = $i * $$heightref / $new_h;
            $source[$i] = int($oldi);
            if ($oldi+1 >= $oldy_block || $oldi + $ratio < $source[$i]+1) {
                $weight1[$i] = 65536;
                $weight2[$i] = 0;
            } else {
                my $temp = ((int($oldi)+1) - $oldi) / $ratio * (3.14159265358979323846264338327950/2);
                $weight1[$i] = int(65536 * sin($temp)*sin($temp) + 0.5);
                $weight2[$i] = 65536 - $weight1[$i];
            }
        }
        $newframe = "";
        for (my $y = 0; $y < $new_h; $y++) {
            my $block = int($y / $y_block);
            my $i = $y % $y_block;
            my $oldy = $block * $oldy_block + $source[$i];
            if ($weight1[$i] == 0x10000) {
                $newframe .= substr($$frameref, $oldy*$Bpl, $Bpl);
            } else {
                for (my $x = 0; $x < $Bpl; $x++) {
                    my $c1 = ord(substr($$frameref, $oldy*$Bpl+$x, 1));
                    my $c2 = ord(substr($$frameref, ($oldy+1)*$Bpl+$x, 1));
                    my $c = $c1*$weight1[$i] + $c2*$weight2[$i] + 32768;
                    $newframe .= chr($c>>16);
                }
            }
        }
        $$frameref = $newframe;
        $$heightref = $new_h;
    }

    if ($resize_w) {
        my $new_w = $$widthref + $resize_w;
        my $ratio = $$widthref / $new_w;
        my $oldx_block = int($$widthref/$mult);
        my $x_block = int($new_w/$mult);
        my (@source, @weight1, @weight2);
        for (my $i = 0; $i < $x_block; $i++) {
            my $oldi = $i * $$widthref / $new_w;
            $source[$i] = int($oldi);
            if ($oldi+1 >= $oldx_block || $oldi + $ratio < $source[$i]+1) {
                $weight1[$i] = 65536;
                $weight2[$i] = 0;
            } else {
                my $temp = ((int($oldi)+1) - $oldi) / $ratio * (3.14159265358979323846264338327950/2);
                $weight1[$i] = int(65536 * sin($temp)*sin($temp) + 0.5);
                $weight2[$i] = 65536 - $weight1[$i];
            }
        }
        $newframe = "";
        for (my $block = 0; $block < $$heightref * $mult; $block++) {
            my $y = int($block/$mult);
            for (my $i = 0; $i < $x_block; $i++) {
                my $oldx = ($block%$mult) * $oldx_block + $source[$i];
                if ($weight1[$i] == 0x10000) {
                    $newframe .= substr($$frameref, ($y*$$widthref+$oldx)*$Bpp,
                                        $Bpp);
                } else {
                    for (my $j = 0; $j < $Bpp; $j++) {
                        my $c1 = ord(substr($$frameref,
                                            ($y*$$widthref+$oldx)*$Bpp+$j,
                                            1));
                        my $c2 = ord(substr($$frameref,
                                            ($y*$$widthref+$oldx+1)*$Bpp+$j,
                                            1));
                        my $c = $c1*$weight1[$i] + $c2*$weight2[$i] + 32768;
                        $newframe .= chr($c>>16);
                    }
                }
            }
        }
        $$frameref = $newframe;
        $$widthref = $new_w;
    }

    return 1;
}

################

# -Z (slow)
# Implemented using triangle filter
sub vidcore_zoom
{
    my ($csp, $frameref, $widthref, $heightref, $newwidth, $newheight) = @_;
    my $Bpp = $csp<0 ? -$csp : $csp==CSP_RGB ? 3 : 1;
    my $Bpl = $$widthref*$Bpp;

    if ($csp == CSP_YUV) {
        my $w2 = int($$widthref/2);
        my $h2 = int($$heightref/2);
        my $Y = substr($$frameref, 0, $$widthref*$$heightref);
        my $U = substr($$frameref, $$widthref*$$heightref, $w2*$h2);
        my $V = substr($$frameref, $$widthref*$$heightref + $w2*$h2, $w2*$h2);
        return 0 if !&vidcore_zoom(-1, \$Y, $widthref, $heightref,
                                   $newwidth, $newheight);
        my $wdummy = $w2;
        my $hdummy = $h2;
        return 0 if !&vidcore_zoom(-1, \$U, \$wdummy, \$hdummy,
                                   int($newwidth/2), int($newheight/2));
        return 0 if !&vidcore_zoom(-1, \$V, \$w2, \$h2,
                                   int($newwidth/2), int($newheight/2));
        $$frameref = $Y . $U . $V;
        return 1;
    }

    my @x_contrib = ();
    my $xscale = $newwidth / $$widthref;
    my $fscale = ($xscale<1 ? 1/$xscale : 1);
    my $fwidth = 1.0 * $fscale;
    for (my $x = 0; $x < $newwidth*$Bpp; $x++) {
        my $center = int($x/$Bpp) / $xscale;
        my $left = ceil($center - $fwidth);
        my $right = floor($center + $fwidth);
        my @contrib = ();
        for (my $i = $left; $i <= $right; $i++) {
            my $weight = ($i-$center) / $fscale;
            $weight = (1 - abs($weight)) / $fscale;
            $weight = 0 if $weight < 0;
            my $n = $i;
            $n = -$n if $n < 0;
            $n = ($$widthref-$n) + $$widthref - 1 if $n >= $$widthref;
            push @contrib, $n*$Bpp + ($x%$Bpp), int($weight*65536);
        }
        push @x_contrib, [@contrib];
    }

    my @y_contrib = ();
    my $yscale = $newheight / $$heightref;
    $fscale = ($yscale<0 ? 1/$yscale : 1);
    $fwidth = 1.0 * $fscale;
    for (my $y = 0; $y < $newheight; $y++) {
        my $center = $y / $yscale;
        my $left = ceil($center - $fwidth);
        my $right = floor($center + $fwidth);
        my @contrib = ();
        for (my $i = $left; $i <= $right; $i++) {
            my $weight = ($i-$center) / $fscale;
            $weight = (1 - abs($weight)) / $fscale;
            $weight = 0 if $weight < 0;
            my $n = $i;
            $n = -$n if $n < 0;
            $n = ($$heightref-$n) + $$heightref - 1 if $n >= $$heightref;
            push @contrib, $n, int($weight*65536);
        }
        push @y_contrib, [@contrib];
    }

    my $tmpframe = "";
    for (my $y = 0; $y < $$heightref; $y++) {
        use integer;
        my @pixels = unpack("C*", substr($$frameref, $y*$Bpl, $Bpl));
        for (my $x = 0; $x < $newwidth*$Bpp; $x++) {
            my $weight = 0x8000;
            for (my $i = 0; $i < @{$x_contrib[$x]}; $i += 2) {
                $weight += $pixels[$x_contrib[$x][$i]] * $x_contrib[$x][$i+1];
            }
            $weight >>= 16;
            $weight = 0 if $weight < 0;
            $weight = 255 if $weight > 255;
            $tmpframe .= chr($weight);
        }
    }

    my @tmpframe = ();
    $Bpl = $newwidth*$Bpp;
    for (my $y = 0; $y < $$heightref; $y++) {
        push @tmpframe, [unpack("C*", substr($tmpframe, $y*$Bpl, $Bpl))];
    }

    my $newframe = "";
    for (my $y = 0; $y < $newheight; $y++) {
        use integer;
        for (my $x = 0; $x < $newwidth*$Bpp; $x++) {
            my $weight = 0x8000;
            for (my $i = 0; $i < @{$y_contrib[$y]}; $i += 2) {
                $weight += $tmpframe[$y_contrib[$y][$i]][$x] * $y_contrib[$y][$i+1];
            }
            $weight >>= 16;
            $weight = 0 if $weight < 0;
            $weight = 255 if $weight > 255;
            $newframe .= chr($weight);
        }
    }

    $$frameref = $newframe;
    $$widthref = $newwidth;
    $$heightref = $newheight;
    return 1;
}

################

# -r
sub vidcore_reduce
{
    my ($csp, $frameref, $widthref, $heightref, $reduce_h, $reduce_w) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;

    my $newframe = "";
    for (my $y = 0; $y < int($$heightref/$reduce_h); $y++) {
        for (my $x = 0; $x < int($$widthref/$reduce_w); $x++) {
            $newframe .= substr($$frameref, (($y*$reduce_h)*$$widthref
                                             +($x*$reduce_w))*$Bpp, $Bpp);
        }
    }
    if ($csp == CSP_YUV) {
        my $ofs = $$widthref * $$heightref;
        for (my $y = 0; $y < int($$heightref/2/$reduce_h); $y++) {
            for (my $x = 0; $x < int($$widthref/2/$reduce_w); $x++) {
                $newframe .= substr($$frameref,
                                    $ofs + (($y*$reduce_h)*int($$widthref/2)
                                            +($x*$reduce_w)), 1);
            }
        }
        $ofs += int($$widthref/2) * int($$heightref/2);
        for (my $y = 0; $y < int($$heightref/2/$reduce_h); $y++) {
            for (my $x = 0; $x < int($$widthref/2/$reduce_w); $x++) {
                $newframe .= substr($$frameref,
                                    $ofs + (($y*$reduce_h)*int($$widthref/2)
                                            +($x*$reduce_w)), 1);
            }
        }
    }
    $$frameref = $newframe;
    $$widthref = int($$widthref / $reduce_w);
    $$heightref = int($$heightref / $reduce_h);
    return 1;
}

################

# -z
sub vidcore_flip_v
{
    my ($csp, $frameref, $widthref, $heightref) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $Bpl = $$widthref * $Bpp;

    my $newframe = "";
    for (my $y = $$heightref-1; $y >= 0; $y--) {
        $newframe .= substr($$frameref, $y*$Bpl, $Bpl);
    }
    if ($csp == CSP_YUV) {
        my $ofs = $$widthref * $$heightref;
        $Bpl = int($Bpl/2);
        for (my $y = int($$heightref/2)-1; $y >= 0; $y--) {
            $newframe .= substr($$frameref, $ofs + $y*$Bpl, $Bpl);
        }
        $ofs += int($$heightref/2) * $Bpl;
        for (my $y = int($$heightref/2)-1; $y >= 0; $y--) {
            $newframe .= substr($$frameref, $ofs + $y*$Bpl, $Bpl);
        }
    }
    $$frameref = $newframe;
    return 1;
}

################

# -l
sub vidcore_flip_h
{
    my ($csp, $frameref, $widthref, $heightref) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;

    my $newframe = "";
    for (my $y = 0; $y < $$heightref; $y++) {
        for (my $x = $$widthref-1; $x >= 0; $x--) {
            $newframe .= substr($$frameref, ($y*$$widthref+$x)*$Bpp, $Bpp);
        }
    }
    if ($csp == CSP_YUV) {
        my $ofs = $$widthref * $$heightref;
        my $Bpl = int($$widthref/2);
        for (my $y = 0; $y < int($$heightref/2) * 2; $y++) {
            for (my $x = $Bpl-1; $x >= 0; $x--) {
                $newframe .= substr($$frameref, $ofs+($y*$Bpl+$x), 1);
            }
        }
    }
    $$frameref = $newframe;
    return 1;
}

################

# -k
sub vidcore_rgbswap
{
    my ($csp, $frameref, $widthref, $heightref) = @_;

    my $newframe = "";
    if ($csp == CSP_RGB) {
        for (my $i = 0; $i < $$widthref*$$heightref; $i++) {
            $newframe .= substr($$frameref, $i*3+2, 1);
            $newframe .= substr($$frameref, $i*3+1, 1);
            $newframe .= substr($$frameref, $i*3  , 1);
        }
    } elsif ($csp == CSP_YUV) {
        my $Ysize = $$widthref * $$heightref;
        my $UVsize = int($$widthref/2) * int($$heightref/2);
        $newframe = substr($$frameref, 0, $Ysize)
                  . substr($$frameref, $Ysize+$UVsize, $UVsize)
                  . substr($$frameref, $Ysize, $UVsize);
    } else {
        return 0;
    }
    $$frameref = $newframe;
    return 1;
}

################

# -K
sub vidcore_grayscale
{
    my ($csp, $frameref, $widthref, $heightref) = @_;

    my $newframe = "";
    if ($csp == CSP_RGB) {
        for (my $i = 0; $i < $$widthref * $$heightref; $i++) {
            my $r = ord(substr($$frameref, $i*3  , 1));
            my $g = ord(substr($$frameref, $i*3+1, 1));
            my $b = ord(substr($$frameref, $i*3+2, 1));
            my $c = (19595*$r + 38470*$g + 7471*$b + 32768) >> 16;
            $newframe .= chr($c) x 3;
        }
    } elsif ($csp == CSP_YUV) {
        $newframe = substr($$frameref, 0, $$widthref * $$heightref)
                  . ("\x80" x (int($$widthref/2) * int($$heightref/2) * 2));
    } else {
        return 0;
    }
    $$frameref = $newframe;
    return 1;
}

################

# -G
sub vidcore_gamma_adjust
{
    my ($csp, $frameref, $widthref, $heightref, $gamma) = @_;
    my $Bpp = ($csp==CSP_RGB ? 3 : 1);

    my @table = ();
    for (my $i = 0; $i < 256; $i++) {
        $table[$i] = int((($i/255)**$gamma) * 255);
    }

    my $newframe = "";
    for (my $i = 0; $i < $$widthref*$$heightref*$Bpp; $i++) {
        $newframe .= chr($table[ord(substr($$frameref, $i, 1))]);
    }
    if ($csp != CSP_RGB) {
        $newframe .= substr($$frameref, $$widthref*$$heightref*$Bpp);
    }
    $$frameref = $newframe;
    return 1;
}

################

# -C
sub vidcore_antialias
{
    my ($csp, $frameref, $widthref, $heightref, $aa_weight, $aa_bias) = @_;
    my $Bpp = $csp==CSP_RGB ? 3 : 1;
    my $Bpl = $$widthref * $Bpp;
    my (@table_c, @table_x, @table_y, @table_d);

    for (my $i = 0; $i < 256; $i++) {
        $table_c[$i] = int($i * $aa_weight * 65536);
        $table_x[$i] = int($i * $aa_bias * (1-$aa_weight)/4 * 65536);
        $table_y[$i] = int($i * (1-$aa_bias) * (1-$aa_weight)/4 * 65536);
        $table_d[$i] = int(($table_x[$i] + $table_y[$i] + 1) / 2);
    }

    my $newframe = substr($$frameref, 0, $Bpl);
    for (my $y = 1; $y < $$heightref-1; $y++) {
        $newframe .= substr($$frameref, $y*$Bpl, $Bpp);
        for (my $x = 1; $x < $$widthref-1; $x++) {
            my $UL = substr($$frameref, (($y-1)*$$widthref+($x-1))*$Bpp, $Bpp);
            my $U  = substr($$frameref, (($y-1)*$$widthref+($x  ))*$Bpp, $Bpp);
            my $UR = substr($$frameref, (($y-1)*$$widthref+($x+1))*$Bpp, $Bpp);
            my $L  = substr($$frameref, (($y  )*$$widthref+($x-1))*$Bpp, $Bpp);
            my $C  = substr($$frameref, (($y  )*$$widthref+($x  ))*$Bpp, $Bpp);
            my $R  = substr($$frameref, (($y  )*$$widthref+($x+1))*$Bpp, $Bpp);
            my $DL = substr($$frameref, (($y+1)*$$widthref+($x-1))*$Bpp, $Bpp);
            my $D  = substr($$frameref, (($y+1)*$$widthref+($x  ))*$Bpp, $Bpp);
            my $DR = substr($$frameref, (($y+1)*$$widthref+($x+1))*$Bpp, $Bpp);
            if ((&SAME($L,$U) && &DIFF($L,$D) && &DIFF($L,$R))
             || (&SAME($L,$D) && &DIFF($L,$U) && &DIFF($L,$R))
             || (&SAME($R,$U) && &DIFF($R,$D) && &DIFF($R,$L))
             || (&SAME($R,$D) && &DIFF($R,$U) && &DIFF($R,$L))
            ) {
                for (my $i = 0; $i < $Bpp; $i++) {
                    my $c = $table_d[ord(substr($UL,$i,1))]
                          + $table_y[ord(substr($U ,$i,1))]
                          + $table_d[ord(substr($UR,$i,1))]
                          + $table_x[ord(substr($L ,$i,1))]
                          + $table_c[ord(substr($C ,$i,1))]
                          + $table_x[ord(substr($R ,$i,1))]
                          + $table_d[ord(substr($DL,$i,1))]
                          + $table_y[ord(substr($D ,$i,1))]
                          + $table_d[ord(substr($DR,$i,1))]
                          + 32768;
                    $newframe .= chr($c>>16);
                }
            } else {
                $newframe .= $C;
            }
        }
        $newframe .= substr($$frameref, $y*$Bpl+($Bpl-$Bpp), $Bpp);
    }
    $newframe .= substr($$frameref, ($$heightref-1)*$Bpl, $Bpl);
    if ($csp != CSP_RGB) {
        $newframe .= substr($$frameref, $$widthref*$$heightref*$Bpp);
    }
    $$frameref = $newframe;
    return 1;
}

sub SAME {
    my @pixel1 = unpack("C*", $_[0]);
    my @pixel2 = unpack("C*", $_[1]);
    my $diff = 0;
    for (my $i = 0; $i < @pixel1; $i++) {
        my $thisdiff = abs($pixel2[$i] - $pixel1[$i]);
        $diff = $thisdiff if $diff < $thisdiff;
    }
    return $diff < 25;
}

sub DIFF { return !&SAME(@_); }

###########################################################################
###########################################################################

# Video data generation.

###########################################################################

sub gen_raw_avi
{
    my ($width,$height,$nframes,$csp,$frame) = @_;

    if (!defined($frame)) {
        $frame = $csp==CSP_RGB ? &gen_rgb_frame($width,$height)
                               : &gen_yuv_frame($width,$height);
    }
    my $frame_chunk = "00db" . pack("V", length($frame)) . $frame;
    my $movi =
        "LIST" .
        pack("V", 4 + length($frame_chunk)*$nframes) .
        "movi" .
        ($frame_chunk x $nframes);

    my $strl =
        "LIST" .
        pack("V", 0x74) .
        "strl" .
        "strh" .
        pack("V", 0x38) .
        "vids" .
        ($csp==CSP_RGB ? "RGB " : "I420") .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 1) .   # frame rate denominator
        pack("V", 25) .  # frame rate numerator
        pack("V", 0) .
        pack("V", $nframes) .
        pack("V", length($frame)) .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0) .
        "strf" .
        pack("V", 0x28) .
        pack("V", 0x28) .
        pack("V", $width) .
        pack("V", $height) .
        pack("vv", 1, 24) .  # planes and bits per pixel, in theory
        ($csp==CSP_RGB ? "RGB " : "I420") .
        pack("V", $width*$height*3) .  # image size, in theory
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0);

    my $hdrl =
        "LIST" .
        pack("V", 0x44 + length($strl)) .
        "hdrl" .
        "avih" .
        pack("V", 0x38) .
        pack("V", 1/25 * 1000000) .  # microseconds per frame
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0x100) .           # AVIF_ISINTERLEAVED
        pack("V", $nframes) .
        pack("V", 0) .
        pack("V", 1) .               # number of streams
        pack("V", 0) .
        pack("V", $width) .
        pack("V", $height) .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0) .
        pack("V", 0) .
        $strl;

    return
        "RIFF" .
        pack("V", 4 + length($hdrl) + length($movi)) .
        "AVI " .
        $hdrl .
        $movi;
}

###########################################################################

# The test frame uses the following colors:
#     RGB (253,0,1)      YUV (81,91,239)
#     RGB (2,255,1)      YUV (145,54,35)
#     RGB (2,0,255)      YUV (41,240,111)
#     RGB (0,0,0)        YUV (16,128,128)   (exact conversion)
#     RGB (85,85,85)     YUV (89,128,128)   (exact conversion)
#     RGB (170,170,170)  YUV (162,128,128)  (exact conversion)
#     RGB (255,255,255)  YUV (235,128,128)  (exact conversion)
# which were chosen because they can be converted accurately between RGB
# and YUV colorspaces with 8 bits per component, assuming rounding.

sub gen_yuv_frame
{
    my ($width,$height) = @_;

    # Size of a vertical color bar (1/4 of the frame, multiple of 16 pixels)
    my $barsize = int($width/64) * 16;
    # Size of the black/gray/white bar (everything remaining)
    my $whitesize = $width - 3*$barsize;
    # Height of top 3 sections (1/4 of the frame, multiple of 16 pixels)
    my $height1 = int($height/64) * 16;
    # Height of the bottom section (everything remaining)
    my $height2 = $height - 3*$height1;

    # Color bar part of Y rows
    my $Yright = chr(81)x$barsize . chr(145)x$barsize . chr(41)x$barsize;
    # Y rows (shades of gray from black to white on the right)
    my $Yrow0 = chr( 16)x$whitesize . $Yright;
    my $Yrow1 = chr( 89)x$whitesize . $Yright;
    my $Yrow2 = chr(162)x$whitesize . $Yright;
    my $Yrow3 = chr(235)x$whitesize . $Yright;
    # U and V rows
    my $Urow = chr(128) x (($width-$barsize*3)/2) .
               chr( 91) x ($barsize/2) .
               chr( 54) x ($barsize/2) .
               chr(240) x ($barsize/2);
    my $Vrow = chr(128) x (($width-$barsize*3)/2) .
               chr(239) x ($barsize/2) .
               chr( 35) x ($barsize/2) .
               chr(111) x ($barsize/2);

    # Y plane
    my $Y = $Yrow0 x $height1 . $Yrow1 x $height1 . $Yrow2 x $height1 .
        $Yrow3 x $height2;
    # U and V planes
    my $U = $Urow x ($height/2);
    my $V = $Vrow x ($height/2);

    # Final frame
    return $Y.$U.$V;
}


sub gen_rgb_frame
{
    my ($width,$height) = @_;

    # Size of a vertical color bar (1/4 of the frame, multiple of 16 pixels)
    my $barsize = int($width/64) * 16;
    # Size of the black/gray/white bar (everything remaining)
    my $whitesize = $width - 3*$barsize;
    # Height of top 3 sections (1/4 of the frame, multiple of 16 pixels)
    my $height1 = int($height/64) * 16;
    # Height of the bottom section (everything remaining)
    my $height2 = $height - 3*$height1;

    # Color bar part of one row
    my $color = (chr(253).chr(0).chr(1)) x $barsize .
                (chr(2).chr(255).chr(1)) x $barsize .
                (chr(2).chr(0).chr(255)) x $barsize;
    # Full rows (shades of gray from black to white on the right)
    my $row0 = chr(0)x($whitesize*3) . $color;
    my $row1 = chr(85)x($whitesize*3) . $color;
    my $row2 = chr(170)x($whitesize*3) . $color;
    my $row3 = chr(255)x($whitesize*3) . $color;

    # Final frame
    return $row0 x $height1 .
           $row1 x $height1 .
           $row2 x $height1 .
           $row3 x $height2;
}

###########################################################################

# Local variables:
#   indent-tabs-mode: nil
# End:
#
# vim: expandtab shiftwidth=4:
