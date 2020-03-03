#!/usr/bin/perl -w

# dependencies: perl ImageMagick mjpegtools

my($testdir) = "transcode-test";
my($frames) = 5;

my(%testdata) =
(
	import =>
	{
	},

	export =>
	{
		ppm =>
		{
			"" =>
			{
				yuv =>
				{
					"output000000.ppm" => "2d366b437ced6b4b82ddab406cfc272d",
					"output000001.ppm" => "a744069a2eaf23f3441a838c5748a746",
					"output000002.ppm" => "4ba2a5195b2c53eb6228f702394d1166",
					"output000003.ppm" => "f4324b1d944d4541948cd8d0221a8c89",
					"output000004.ppm" => "f01e6f29b62abf96f0ef58ba34328fa5"
				},

				rgb =>
				{
					"output000000.ppm" => "415f11d450796f2a0c181b4fe2bba462",
					"output000001.ppm" => "920db15b73c796c31b11dade8e802a1c",
					"output000002.ppm" => "c0dbe650840584e222df6de880debe1d",
					"output000003.ppm" => "f0fa9f76d284f3082ffdb2d4790de300",
					"output000004.ppm" => "bfd12d139827eab1974a8b32f871d5d2"
				}
			}
		},

		ffmpeg =>
		{
			"mpeg2" =>
			{
				yuv =>
				{
					"output.m2v"	=>	"58a158043a2ed9dce23c5892af4fe39d"
				},

				rgb =>
				{
					"output.m2v"	=>	"af88136aaa2e5c69a1cd418e15d6ef0f"
				}
			},

			"mpeg4" =>
			{
				yuv =>
				{
					"output"	=>	"350c502d87ed9e4a3bb8c1567f8cdc3e"
				},

				rgb =>
				{
					"output"	=>	"6026db8ebfdefccbdc5f4c37f5ac3938"
				}
			}
		}
	},

	filter =>
	{
	}
);

sub checkfiles($ $ $ $)
{
	my($type, $module, $codec, $mode) = @_;
	my($file, $fsfile, $file1, $sum, $sum1, $result);

	$result = 0;

	for $file (sort(keys(%{$testdata{$type}{$module}{$codec}{$mode}})))
	{
		$fsfile = "$module-$codec-$mode-$file";

		printf("  Checking file %s: ", $file);

		if(! -f $fsfile)
		{
			printf("FAILED (file not found)\n");
			$result = 1;
			next;
		}

		$sum = $testdata{$type}{$module}{$codec}{$mode}{$file};

		($sum1, $file1) = split(" ", qx[md5sum $fsfile]);

		if($sum ne $sum1)
		{
			printf("FAILED (bad checksum) [%s != %s]\n", $sum, $sum1);
			$result= 1;
			next;
		}

		printf("OK\n");
	}

	return($result);
}

sub testmodule($ $ $ $ $ $ $)
{
	my($type, $import_module, $import_file, $export_module, $export_file, $mode, $codec) = @_;
	my($cmdline, $fsfile);

	$fsfile = "$export_module-$codec-$mode-$export_file";

	if($type ne "filter")
	{
		$cmdline = "transcode -H 0 -c 0-5 -i $import_file -x $import_module,null -o $fsfile -y $export_module,null >> log 2>&1";
	}
	else
	{
		return(1);
	}

	$cmdline .= " -V rgb24" if($mode eq "rgb");
	$cmdline .= " -F $codec" if(defined($codec) && $codec ne "");

	open(my($fd), ">>log");
	printf $fd (">>> $cmdline\n");
	close($fd);

	return(1) if(system($cmdline));

	return(0);
}

sub main($)
{
	my($frame, $name, @allnames, @report);
	my($module, $mode, $sum, $result);
	my($import_module, $import_file, $export_module, $export_file);

	mkdir($testdir);
	chdir($testdir);

	unlink("log");

	if(! -f "testimg.ppm")
	{
		printf("Generating base testimage\n");
		if(system(q[convert xc:black -resize 720x576! \
				-fill red   -draw "rectangle 0,0,213,480" \
				-fill green -draw "rectangle 214,0,426,480" \
				-fill blue  -draw "rectangle 427,0,640,480" \
				-fill white -font "-*-helvetica-bold-r-*-*-24-*-*-*-*-*-*-1" \
				-fill white -draw "text 100,240 'RED'" \
			            	-draw "text 313,240 'GREEN'" \
			            	-draw "text 526,240 'BLUE'" -quality 100 testimg.ppm >> log 2>&1]))
		{
			die("convert");
		}
	}

	for($frame = 0; $frame < $frames; $frame++)
	{
		$name = sprintf("testframe-%3.3d.ppm", $frame);

		if(! -f $name)
		{
			printf("Generating test frame $name\n");

			if(system(q[convert xc:black -resize 720x576! \
					-fill red   -draw "rectangle 0,0,213,480" \
					-fill green -draw "rectangle 214,0,426,480" \
					-fill blue  -draw "rectangle 427,0,640,480" \
					-fill white -font "-*-helvetica-bold-r-*-*-24-*-*-*-*-*-*-1" \
					-fill white -draw "text 100,240 'RED'" \
			            		-draw "text 313,240 'GREEN'" \
			            		-draw "text 526,240 'BLUE'" \
								-draw "text 300,400] . qq['Frame #$frame'" ] .
								qq[-format ppm $name >> log 2>&1]))
			{
				die("convert");
			}
		}

		push(@allnames, $name);
	}

	if(! -f "testmovie.yuv4mpeg")
	{
		printf("Generating test movie\n");

		if(system("cat " . join(" ", @allnames) . " | ppmtoy4m -F 25:1 -A 59:54 -I t -L -S 420mpeg2 > testmovie.yuv4mpeg 2>> log"))
		{
			die("ppmtoy4m");
		}
	}

	if(qx[md5sum testmovie.yuv4mpeg 2>> log] !~ /^485df9fc04b510eb9d09af1d0b445307\b/)
	{
		die("testmovie.yuv4mpeg md5sum incorrect");
	}

	for $type (sort(keys(%testdata)))
	{
		for $module (sort(keys(%{$testdata{$type}})))
		{
			for $codec (sort(keys(%{$testdata{$type}{$module}})))
			{
				for $mode (sort(keys(%{$testdata{$type}{$module}{$codec}})))
				{
					printf("Testing %s_%s in %s mode\n", $type, $module, $mode);

					if($type eq "import")
					{
						$import_module	=	$module;
						$import_file	=	"testmovie.$import_module";
						$export_module	=	"yuv4mpeg";
						$export_file	=	"output";
					}
					else
					{
						if($type eq "export")
						{
							$import_module	=	"yuv4mpeg";
							$import_file	=	"testmovie.yuv4mpeg";
							$export_module	=	$module;
							$export_file	=	"output";
						}
						else
						{
							# filter
						}
					}

					$result = testmodule($type, $import_module, $import_file, $export_module, $export_file, $mode, $codec);
					$result = checkfiles($type, $module, $codec, $mode) if(!$result);
					push(@report, sprintf("%-16.16s %8.8s %4.4s %6.6s", $type . "_" . $module, $codec, $mode, $result ? "FAILED" : "OK"));
				}
			}
		}
	}

	printf("\n");
	printf("%-16.16s %8.8s %-4.4s %-6.6s\n", "module", "codec", "mode", "result");
	printf("%s %s %s %s\n", "-" x 16, "-" x 8, "-" x 4, "-" x 6);
	printf(join("\n", @report));
	printf("\n%s\n", "-" x 37);

	open(my($fd), ">>log");
	printf $fd ("\n");
	printf $fd ("%-16.16s %8.8s %-4.4s %-6.6s\n", "module", "codec", "mode", "result");
	printf $fd ("%s %s %s %s\n", "-" x 16, "-" x 8, "-" x 4, "-" x 6);
	printf $fd (join("\n", @report));
	printf $fd ("\n%s\n", "-" x 37);
	close($fd);

}

exit(main(%ARGV));
