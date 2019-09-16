#!/bin/sh

led_1=19
led_2=13
led_3=6
led_4=5
led_5=0

if [ "x$(which gpio)" = "x" ]
then
	echo "No 'gpio' command found! Install wiringpi."
	exit 1
fi

all_off() {
	gpio -g write ${led_1} 0
	gpio -g write ${led_2} 0
	gpio -g write ${led_3} 0
	gpio -g write ${led_4} 0
	gpio -g write ${led_5} 0
}

green_on() {
	all_off
	gpio -g write ${green_led} 1
}

yellow_on() {
	all_off
	gpio -g write ${yellow_led} 1
}

red_on() {
	all_off
	gpio -g write ${red_led} 1
}

red_also_on() {
	gpio -g write ${red_led} 1
}

led_on() {
	case $1 in
	1) gpio -g write ${led_1} on ;;
	2) gpio -g write ${led_2} on ;;
	3) gpio -g write ${led_3} on ;;
	4) gpio -g write ${led_4} on ;;
	5) gpio -g write ${led_5} on ;;
	esac
}

led_off() {
	case $1 in
	1) gpio -g write ${led_1} off ;;
	2) gpio -g write ${led_2} off ;;
	3) gpio -g write ${led_3} off ;;
	4) gpio -g write ${led_4} off ;;
	5) gpio -g write ${led_5} off ;;
	esac
}

gpio_setup() {
	gpio -g mode ${led_1} out
	gpio -g mode ${led_2} out
	gpio -g mode ${led_3} out
	gpio -g mode ${led_4} out
	gpio -g mode ${led_5} out
	for i in $(seq 1 5)
	do
		all_off
		led_on $i
		sleep .2
	done

	all_off
	led_on 1
	led_on 2
	sleep 1

	all_off
	led_on 5
}

fail_test() {
	led_off 2
	led_off 3
	led_off 4

	case $1 in
	load-tester-bitstream)                                 ;;
	run-all-tests)          led_on 2                       ;;
	validate-spi)                      led_on 3 ; led_on 4 ;;
	test-spi)                          led_on 3 ;;
	test-rgbg)              led_on 2 ; led_on 3 ;;
	test-rgbb)              led_on 2 ; led_on 3 ;;
	test-rgbr)              led_on 2 ; led_on 3 ;;
	test-touch)                        led_on 3 ; led_on 4 ;;
	load-final-bitstream)                         led_on 4 ;;
	verify-final-bitstream)                       led_on 4 ;;
	esac
}

gpio_setup

echo "HELLO bash-ltc-jig 1.0"
while read line
do
	if echo "${line}" | grep -iq '^start'
	then
		all_off
		led_on 2
		led_on 3
		led_on 4
		failures=0
	elif echo "${line}" | grep -iq '^fail'
	then
		if [ $failures -ne 1 ]
		then
			failures=1
			fail_test $(echo "${line}" | awk '{print $2}')
		fi
		led_on 1
	elif echo "${line}" | grep -iq '^finish'
	then
		led_on 5
		result=$(echo ${line} | awk '{print $3}')
		if [ ${result} -ge 200 -a ${result} -lt 300 ]
		then
			led_off 2
			led_off 3
			led_off 4
		else
			led_on 1
		fi
	elif echo "${line}" | grep -iq '^exit'
	then
		all_off
		exit 0
	fi
done
