#!/bin/sh
# Walk the stock Settings row and record what each position launches.
#
# The row cannot be walked by hand once a tile lands on screen 1016, because
# that screen swallows every subsequent key in the no-satellite state. Every
# position therefore starts from a fresh gotoScreenById 10306, which is a
# DruidTester command and so is immune to the swallowed keys. The log line
# Druid already prints on a screen change gives the numeric id, so this script
# never has to guess.

UC=/opt/middleware_core/system/tv/uconntest
LG=/var/viewer/messages.log
SID='<com.directv.druid.dt.DruidTester command="getCurrentScreenId" session="local"/>'

screen() {
	$UC "$SID" 2>/dev/null | tail -1
}

goto() {
	$UC "<com.directv.druid.dt.DruidTester command=\"gotoScreenById\" screenId=\"$1\" session=\"local\"/>" >/dev/null 2>&1
}

key() {
	wget -q -O /dev/null \
		"http://127.0.0.1:8080/remote/processKey?key=$1&hold=keyPress"
}

POSITIONS="${1:-6}"
SETTINGS_SCREEN="${2:-10306}"

pos=0
while [ "$pos" -lt "$POSITIONS" ]; do
	goto "$SETTINGS_SCREEN"
	sleep 3

	i=0
	while [ "$i" -lt "$pos" ]; do
		key right
		sleep 1
		i=$((i + 1))
	done

	mark=$(wc -l < "$LG")
	key select
	sleep 4
	end=$(wc -l < "$LG")

	echo "=== position $pos: $(screen)"
	tail -n +$((mark + 1)) "$LG" | grep -a -E "Loading screenID|Loaded screenID|Next Screen|now loading|Now loading|Exception|HAI" | head -8
	pos=$((pos + 1))
done
