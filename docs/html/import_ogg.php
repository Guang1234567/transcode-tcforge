<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<?php


$mod = "import_ogg.so";
$des = "decoder frontend for OSS Ogg Vorbis audio";
$aut = "ThOe";
$ver = "0.6.0rc4";
$pac = "Open-Source available from the <a href=http://www.xiph.org/ogg/vorbis/>Ogg Vorbis CODEC project</a>.";
$format[0] = "-------";
$format[1] = "Ogg Vorbis";
$file = "Ogg bitstream (*.ogg)";
$c[0] = "raw audio import with -p &lt;filename&gt; or auto-probing";
$c[1] = "needs <i>oggdec</i>.";

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
