#!/bin/sh
HGVER=`hg identify -i | tr -d '+\n'`
if [ -z "$HGVER" ]; then
	HGVER="unknown"
fi
echo -n $1$HGVER

