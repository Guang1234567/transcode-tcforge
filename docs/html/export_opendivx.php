<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<?php


$mod = "export_opendivx.so";
$des = "encoder frontend for OpenDivX video based on MPEG4 v2";
$aut = "ThOe";
$ver = "0.2.0";
$pac = "-";
$format[0] = "OpenDivX (FourCC=DIVX)";
$format[1] = "MP3/AC3/PCM";
$file = "AVI";
$c[0] = "obsolete";
$c[1] = "option -r does not work correctly for PAL (720x576) videos. <br> The frame must be cropped and the following combinations work: -r2 or -r4 with -j 0,8 and -r8 -j 32,40";
$c[2] = "raw audio export with -m";

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
