# This file is part of the lightdhcp project
# 
# (C) 2020 Andreas Steinmetz, ast@domdv.de
# The contents of this file is licensed under the GPL version 2 or, at
# your choice, any later version of this license.
#
DHCPCD=8.1.6

all: dhcpcd lightdhcp

lightdhcp: lightdhcp.c
	gcc -Wall -Os -s -o lightdhcp lightdhcp.c

dhcpcd: dhcpcd-dhcpcd-$(DHCPCD)/src/dhcpcd
	cp dhcpcd-dhcpcd-$(DHCPCD)/src/dhcpcd dhcpcd
	strip dhcpcd

dhcpcd-dhcpcd-$(DHCPCD)/src/dhcpcd: dhcpcd-dhcpcd-$(DHCPCD)/config.h
	make -C dhcpcd-dhcpcd-$(DHCPCD)

dhcpcd-dhcpcd-$(DHCPCD)/config.h: dhcpcd-dhcpcd-$(DHCPCD)/configure.sh
	cd dhcpcd-dhcpcd-$(DHCPCD) && chmod +x ./configure.sh
	cd dhcpcd-dhcpcd-$(DHCPCD) && ./configure.sh

dhcpcd-dhcpcd-$(DHCPCD)/configure.sh: dhcpcd-$(DHCPCD).tar.gz \
	dhcpcd-$(DHCPCD).patch
	tar xf dhcpcd-$(DHCPCD).tar.gz
	patch -p1 -d dhcpcd-dhcpcd-$(DHCPCD) < dhcpcd-$(DHCPCD).patch

clean:
	rm -rf dhcpcd-dhcpcd-* dhcpcd lightdhcp
