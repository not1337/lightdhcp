      lightdhcp, a dhcpcd controlling daemon for reduced latency
                   (c) 2020 Andreas Steinmetz

--------------------------------------------------------------------------


  A daemon for reduced network latency when using dhcpcd
===========================================================

If you're fighting network latency spikes on e.g. a Raspberry Pi and are
in need of two digit microsecond network roundtrip latency then this is
for you.

The Raspberry Pi 4B is capable of fast networking with an average
roundtrip delay of less than 50us between two such systems running a
userspace measurement tool. There are, however, sudden latency spikes
with a latency increase in the 50-150us range which are caused by
fork/exec as well as by disk I/O (even when cached or using tmpfs).

One of the major reason of those spikes is the DHCP daemon configuring
the system and thus running a bunch of scripts. Very light DHCP daemons
that do not run scripts do however lack features, e.g. IPv6 support.
Note that a RT kernel will not improve the situation but instead will
cause general increased network latency.

Thus, here is a patch to dhcpcd and a controlling daemon that makes
the combination of controller, dhcpcd and optionally chrony fork free.
File I/O of dhcpcd and the controlling daemon is reduced to the
absolute minimum too. Note that if you are using a precision RTC with a
PPS signal you should not use chrony but synchronize the kernel
with the PPS signal directly (needs non-tickless kernel with
proper PPS configuration as well as ppsctl from pps-tools),
if you can accept a few seconds drift in one year.

After building the code first run 'lightdhcp -h' for available options.
You will need to install this stuff manually as you can easily break
network access to your embedded system if something goes wrong.
Which means: you installed it willingly and manually, so you broke it.

The code is tested in so far as to suit my personal requirements.
Thus there may be bugs lurking in parts of the code I don't use.
