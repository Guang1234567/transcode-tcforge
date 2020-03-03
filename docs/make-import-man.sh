#!/bin/bash

cat << EOF
<?xml version="1.0" encoding="ISO-8859-1"?>
<!DOCTYPE refentry PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN"
 "http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd">

<refentry id='transcode_import'>

    <refentryinfo>
        <date>14th July 2008</date>
    </refentryinfo>

    <refmeta>
        <refentrytitle>transcode_import</refentrytitle>
        <manvolnum>1</manvolnum>
        <refmiscinfo class='date'>15th April 2008</refmiscinfo>
        <refmiscinfo class='source'>transcode_import(1)</refmiscinfo>
    </refmeta>
    
    <refnamediv id='name'>
        <refname>transcode_import</refname>
        <refpurpose>transcode import modules collection</refpurpose>
    </refnamediv>
 
    <!-- body begins here -->
    <refsynopsisdiv id='synopsis'>
        <cmdsynopsis>
            <command>transcode</command>    
            <arg choice='plain'>
                -x <replaceable>name</replaceable>
                <arg choice='opt'>
                    <arg choice='plain'>
                        <replaceable>,name</replaceable>
                    </arg>
                </arg>
            </arg>
            <arg choice='opt'>
                <replaceable>other options</replaceable>
            </arg>
        </cmdsynopsis>
    </refsynopsisdiv>
    
    <refsect1 id='copyright'>
        <title>Copyright</title>
        <para>
	    <command>transcode</command> is Copyright (C) 2001-2003 by Thomas Oestreich, 2003-2004 Tilmann Bitterberg, 2004-2010 Transcode Team
        </para>
    </refsect1>

    <refsect1 id="import_modules">
        <title>import modules</title>
	<para>
	    If no module is specified through the -x option, <command>transcode</command> will autodetect them using internal probing code.
	    If just one import module is specified, it is used both for video and audio import; if both modules are specified, the first
	    is used for video import, the second for audio import.
            To see what import modules are avalaible for your transcode installation, do a

           <literallayout>
           $ ls -1 \$( tcmodinfo -p )/import*.so
           </literallayout>

           A complete transcode installation has the following import modules.
        </para>
	<variablelist>

EOF
./tcmkmodhelp.py -s ../import/import_*.c ../import/*/import_*.c
cat << EOF



        </variablelist>
    </refsect1>
 
    <refsect1 id='authors'>
        <title>Authors</title>
        <para>
            Written by Thomas Oestreich &lt;ostreich@theorie.physik.uni-goettingen.de&gt;, 
            Tilmann Bitterberg and the Transcode-Team
        </para>
        <para>
            See the <emphasis>AUTHORS</emphasis> file for details.
        </para>
    </refsect1>
    
    <refsect1 id='see_also'>
        <title>See Also</title>
        <para>
             <citerefentry>
                <refentrytitle>transcode</refentrytitle><manvolnum>1</manvolnum>
            </citerefentry>
            ,
            <citerefentry>
                <refentrytitle>tcmodinfo</refentrytitle><manvolnum>1</manvolnum>
            </citerefentry>
            ,
            <citerefentry>
                <refentrytitle>transcode_filter</refentrytitle><manvolnum>1</manvolnum>
            </citerefentry>
            ,
            <citerefentry>
                <refentrytitle>transcode_export</refentrytitle><manvolnum>1</manvolnum>
            </citerefentry>
        </para>
        <!-- .br -->
    </refsect1>
    
</refentry>
EOF

