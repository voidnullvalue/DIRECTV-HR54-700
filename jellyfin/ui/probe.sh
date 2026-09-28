#!/bin/sh
# Receiver-side navigation probe.
#
# Reads a key script on stdin, one directive per line:
#   key <name>        send a keypress through the local SHEF server
#   wait <seconds>    sleep
#   screen            print the current Druid screen id
#   mark              remember the current messages.log line count
#   log [pattern]     print messages.log lines added since the last mark
#
# Every log dump is bracketed by the line range it covered so a caller can tell
# fresh output from leftovers, because messages.log is append-only and shared
# with the rest of the middleware.

UC=/opt/middleware_core/system/tv/uconntest
LG=/var/viewer/messages.log
SID='<com.directv.druid.dt.DruidTester command="getCurrentScreenId" session="local"/>'
MARK=1

screen() {
	$UC "$SID" 2>/dev/null | tail -1
}

sendkey() {
	wget -q -O /dev/null \
		"http://127.0.0.1:8080/remote/processKey?key=$1&hold=keyPress"
}

logdump() {
	pat="${1:-.}"
	end=$(wc -l < "$LG")
	echo "--- log lines $((MARK + 1))..$end ---"
	if [ "$end" -gt "$MARK" ]; then
		tail -n +$((MARK + 1)) "$LG" | grep -a -E "$pat" | head -40
	fi
	MARK=$end
}

while read -r verb arg; do
	case "$verb" in
	key)
		echo "== key $arg"
		sendkey "$arg"
		sleep 2
		;;
	wait)
		echo "== wait $arg"
		sleep "$arg"
		;;
	screen)
		echo "== screen: $(screen)"
		;;
	mark)
		MARK=$(wc -l < "$LG")
		echo "== marked at line $MARK"
		;;
	log)
		logdump "${arg:-.}"
		;;
	''|\#*) ;;
	*)
		echo "unknown directive: $verb $arg" >&2
		;;
	esac
done

echo "== final screen: $(screen)"
