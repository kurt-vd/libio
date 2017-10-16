#!/bin/sh

POWERLED=/sys/class/leds/power

case "$1" in
services)
	echo "24V direct veluxg velux fancy abort"
	;;
24V)
	exec io resd unix:@24V 2.2
	;;
direct)
	# main io switches to control outputs
	exec io hadirect -l unix:@ha2 \
		+led=oled \
		+zolder=ozolder izolder \
		+fan=ofan ibad1 \
		+lavabo=olavabo \
		+bad=obad ibad4 \
		+bluebad=oblubad \
		+main=omain \
		+blueled=obluslp \
		+hal=ohal igang1
	;;
veluxg)
	# velux gordijn
	exec io hamotor -lunix:@veluxg -i iveluxg1 -i iveluxg2 -i iveluxg3 +hi=oveluxhg +lo=oveluxlg
	;;
velux)
	# velux gordijn
	exec io hamotor -lunix:@velux -i ivelux1 -i ivelux2 -i ivelux3 +hi=oveluxh +lo=oveluxl
	;;
fancy)
	exec io ha2addons -l unix:@ha2+
	;;
abort)
	exec io haspawn -d2 SW1 poweroff
	;;
*)
	echo "usage: $0 [`$0 services`]"
	exit 1
	;;
esac
