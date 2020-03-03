<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<?php


$mod = "export_divx4.so / export_divx4raw.so";
$des = "encoder frontend for DivX 4.xx video";
$aut = "ThOe<br> 2-pass encoding (-R 1/2) by Christoph Lampert<br> -R 3 (constant quantizer mode) added by Gerhard Monzel";
$ver = "0.5.0-pre1";
$pac = "<a href=http://www.divx.com/divx/maclinux.php>DivX 4.xx</a> codec binary for linux (latest linux version is 4.02)";
$format[0] = "DivX 4.xx (FourCC=DIVX)";
$format[1] = "MP3/AC3/PCM";
$file = "AVI or raw bitstream with \"-y divx4raw\"";
$c[0] = "option -r does not work correctly for PAL (720x576) videos. <br> The frame must be cropped and the following combinations work: -r2 or -r4 with -j 0,8 and -r8 -j 32,40";
$c[1] = "raw audio export with -m";
$c[2] = "-y divx4raw requires audio export to separate file with -m";

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
