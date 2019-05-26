#!/bin/sh

infofile=/tmp/spi-info.txt
rm -f $infofile
fomu-flash -t 4 -i | tee $infofile

# Manufacturer ID: Macronix (c2)
# Memory model: MX25R1635F (28)
# Memory size: 16 Mbit (15)
# Device ID: 15

error_count=0
if ! grep -q 'Manufacturer ID: Macronix (c2)' $infofile
then
	echo -n "Unrecognized SPI manufacturer: "
	grep 'Manufacturer ID: ' $infofile
	error_count=$(($error_count+1))
fi

if ! grep -q 'Memory model: MX25R1635F (28)' $infofile
then
	echo -n "Unrecognized memory model: "
	grep 'Memory model: ' $infofile
	error_count=$(($error_count+1))
fi

if ! grep -q 'Memory size: 16 Mbit (15)' $infofile
then
	echo -n "Unrecognized memory size: "
	grep 'Memory size: ' $infofile
	error_count=$(($error_count+1))
fi

if ! grep -q 'Device ID: 15' $infofile
then
	echo -n "Unrecognized device id: "
	grep 'Device ID: ' $infofile
	error_count=$(($error_count+1))
fi

exit $error_count
