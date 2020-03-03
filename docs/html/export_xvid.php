<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<?php


$mod = "export_xvid.so / export_xvidcvs.so /  export_xvidraw.so";
$des = "encoder frontend for XviD video";
$aut = "ThOe<br> 2-pass encoding (-R 1/2) by Christoph Lampert<br> -R 3 (constant quantizer mode) added by Gerhard Monzel";
$ver = "0.5.1";
$pac = "Open-Source available from the <a href=http://www.videocoding.de/index.php?section=Download> XviD project page</a>";
$format[0] = "XviD (FourCC=DIVX)";
$format[1] = "MP3/AC3/PCM";
$file = "AVI / raw bitstream";
$c[0] = "superseeds OpenDivX codec.";
$c[1] = "video frame size parameter should be multiple of 16.";
$c[2] = "option -r does not work correctly for PAL (720x576) videos. <br> The frame must be cropped and the following combinations work: -r2 or -r4 with -j 0,8 and -r8 -j 32,40.";
$c[3] = "raw audio export with -m &lt;filename&gt;.";
$c[4] = "use \"-y xvid\" with codec snapshot of 2002-04-12.";
$c[5] = "<i>export_xvidcvs.so</i> tries to follow latest CVS development.";
$c[6] = "<i>export_xvidraw.so</i> writes raw bitstream for post-processing.";

?>
<!-- /////////////////////////////////////// -->


<html> <head>
<title><?php echo("$mod"); ?></title>
</head>

<body>

<?php
include("table.php");
?>

<hr>
<address></address>
<!-- hhmts start -->
Last modified: Tue Feb  5 15:06:07 CET 2002
<!-- hhmts end -->
</body> </html>
