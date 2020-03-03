<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<?php


$mod = "export_fame.so";
$des = "encoder frontend for MPEG-4 codec provided by <i>libfame</i>";
$aut = "Yannick Vignon <ye.vignon@enst-bretagne.fr>";
$ver = "0.6.0-pre3";
$pac = "Open-Source library <a href=http://sourceforge.net/projects/fame>project homepage</a>";
$format[0] = "DivX (FourCC=DIVX)";
$format[1] = "MP3/AC3/PCM";
$file = "AVI";
$c[0] = "<i>libfame</i> CVS version 0.8.10 required.<br>
Alexander Gloeckner has made some packages available <a href=http://www-user.tu-chemnitz.de/~alg/rpms/>here</a>.";
$c[1] = "only YUV mode -V supported";
$c[2] = "option -r does not work correctly for PAL (720x576) videos. <br> The frame must be cropped and the following combinations work: -r2 or -r4 with -j 0,8 and -r8 -j 32,40";
$c[3] = "raw audio export with -m";

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
