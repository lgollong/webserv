#!/bin/sh

if [ "$QUERY_STRING" = "stall" ]; then
	echo "$$" > /tmp/webserv-cgi-stall.pid
	exec /bin/sleep 60
fi

if [ "$QUERY_STRING" = "close-input" ]; then
	exec 0<&-
	/bin/sleep 1
	printf "Content-type: text/html\r\n\r\n"
	printf "stdin was closed\n"
	exit 0
fi

BODY=""
if [ "${CONTENT_LENGTH:-0}" -gt 0 ]; then
	BODY=$(head -c "$CONTENT_LENGTH")
fi

if [ "$QUERY_STRING" = "delayed" ]; then
	/bin/sleep 1
fi

printf "Content-type: text/html\r\n\r\n"
if [ "$QUERY_STRING" = "large" ]; then
	i=0
	while [ "$i" -lt 20000 ]; do
		printf "x"
		i=$((i + 1))
	done
	exit 0
fi

printf "You sent: %s\n" "$BODY"
