#!/bin/bash
exec indent -bad -bap -bbo -br -brs -l76 -lc76 \
            -lp -nprs -npcs -nut -sai -saf -saw \
	    -ts4 -i4 -ss $@
