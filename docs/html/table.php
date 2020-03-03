<body bgcolor=#CDB5CD>

<?php

$MOD = "module name:";
$DES = "description:";
$AUT = "author(s):";
$PAC = "required packages:";
$AFORM = "audio format:";
$VFORM = "video format:";
$FILE = "file type:";
$COM = "comments:";
$VER = "included since:";


echo "<table cellspacing=10 cellpadding=0 border=0 width=100%>";

echo "<tr>";
echo "<td align=left valign=top width=30% bgcolor=#a0a0a0>";

echo "<table border=0 cellpadding=10 cellspacing=3 font size=+2 bgcolor=#ffffff width=100%>";

echo "<tr>";
echo "<td align=left bgcolor=#e9e9e9 width=30%>";
echo "<FONT FACE=Helvetica font size=+2> $MOD";
echo "</td>";
echo "<td align=left bgcolor=#e9e9e9 width=70%>";
echo "<strong>";
echo "<FONT FACE=Helvetica font size=+2> $mod</strong>";
echo "</td>";
echo "</tr>";

echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $DES";
echo "</td>";
echo "<td align=left>";
echo "<FONT FACE=Helvetica font size=+2> $des";
echo "</td>";
echo "</tr>";


echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $AUT";
echo "</td>";
echo "<td align=left>";
echo "<FONT FACE=Helvetica font size=+2> $aut";
echo "</td>";
echo "</tr>";

echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $VER";
echo "</td>";
echo "<td align=left>";
echo "<FONT FACE=Helvetica font size=+2> $ver";
echo "</td>";
echo "</tr>";


echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $PAC";
echo "</td>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $pac";
echo "</td>";
echo "</tr>";



echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $VFORM";
echo "</td>";
echo "<td align=leftvalign=top >";
echo "<FONT FACE=Helvetica font size=+2> $format[0]";
echo "</td>";
echo "</tr>";

echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $AFORM";
echo "</td>";
echo "<td align=leftvalign=top >";
echo "<FONT FACE=Helvetica font size=+2> $format[1]";
echo "</td>";

echo "</tr>";



echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $FILE";
echo "</td>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $file";
echo "</td>";
echo "</tr>";

echo "<tr>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2> $COM";
echo "</td>";
echo "<td align=left valign=top>";
echo "<FONT FACE=Helvetica font size=+2>";
echo "<ul>";

$i=0;

while ($i<count($c)) {
    echo "<li>";
    echo "$c[$i]";
    echo "</li>";
    ++$i;
}

echo "</ul>";
echo "</td>";
echo "</tr>";




echo "</table>";
echo "</table>";
?>



