<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<?php


$mod = "export_ogg.so";
$des = "encoder frontend for OSS Ogg Vorbis audio";
$aut = "Tilmann Bitterberg";
$ver = "0.6.0rc4";
$pac = "Open-Source available from the <a href=http://www.xiph.org/ogg/vorbis/>Ogg Vorbis CODEC project</a>.";
$format[0] = "-------";
$format[1] = "Ogg Vorbis";
$file = "Ogg bitstream (*.ogg)";
$c[0] = "raw audio export with -m &lt;filename&gt;.";
$c[1] = "needs <i>oggenc</i>.";
$c[2] = "you need <i>oggmerge</i> part of <a href=http://www.bunkus.org/videotools/ogmtools/>ogmtools</a> by Moritz Bunkus to build an Ogg compliant Video/Audio stream.";

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
