#!/bin/sh

infofile=/tmp/spi-info.txt
rm -f $infofile
./fomu-flash -i | tee $infofile

# Manufacturer ID: Macronix (c2)
# Memory model: MX25R1635F (28)
# Memory size: 16 Mbit (15)
# Device ID: 15
check_mx25r16() {
	local infofile="$1"
	local error_count=0
	if ! grep -q 'Manufacturer ID: Macronix (c2)' $infofile
	then
		error_count=$(($error_count+1))
	fi

	if ! grep -q 'Memory model: MX25R1635F (28)' $infofile
	then
		error_count=$(($error_count+1))
	fi

	if ! grep -q 'Memory size: 16 Mbit (15)' $infofile
	then
		error_count=$(($error_count+1))
	fi

	if ! grep -q 'Device ID: 15' $infofile
	then
		error_count=$(($error_count+1))
	fi
	return $error_count
}

# Manufacturer ID: Giga Device (c8)
# Memory model: GD25Q16C (40)
# Memory size: 16 Mbit (15)
# Device ID: 14
check_gd25q16() {
	local infofile="$1"
	local error_count=0
	if ! grep -q 'Manufacturer ID: Giga Device (c8)' $infofile
	then
		error_count=$(($error_count+1))
	fi

	if ! grep -q 'Memory model: GD25Q16C (40)' $infofile
	then
		error_count=$(($error_count+1))
	fi

	if ! grep -q 'Memory size: 16 Mbit (15)' $infofile
	then
		error_count=$(($error_count+1))
	fi

	if ! grep -q 'Device ID: 14' $infofile
	then
		error_count=$(($error_count+1))
	fi
	return $error_count
}

if ! check_mx25r16 $infofile
then
	if ! check_gd25q16 $infofile
	then
		echo "!!! Unrecognized SPI flash" 1>&2
		exit 1
	fi
fi
exit 0
