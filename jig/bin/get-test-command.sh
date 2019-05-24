#!/bin/sh

# Remove any existing serial ports
#if [ -e /dev/ttyACM* ]
#then
#	rm -f /dev/ttyACM*
#fi

fomu-flash -f ~/tester/bin/pvt-test-bitstream.bin 2> /dev/null

# Wait for serial port to appear
while [ ! -e /dev/ttyACM* ]
do
	true
done

sleep .1
stty -F /dev/ttyACM0 raw
timeout 5 perl -e 'while (<>) { print; last if /^FOMU:/; }' < /dev/ttyACM0
