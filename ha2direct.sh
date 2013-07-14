#!/bin/sh

exec io hadirect -l unix:@ha2 \
	+led=oled \
	+zolder=ozolder izolder \
	+fan=ofan ibad1 \
	+lavabo=olavabo ihead2 \
	+bad=obad ibad4 \
	+bluebad=oblubad ibad3 \
	+main=omain iwest3 ihead3 \
	+blueled=obluslp iwest4 \
	+hal=ohal igang1
