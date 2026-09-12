#!/bin/sh

BODY=""
if [ "${CONTENT_LENGTH:-0}" -gt 0 ]; then
	BODY=$(head -c "$CONTENT_LENGTH")
fi

if [ "$QUERY_STRING" = "stall" ]; then
	echo "$$" > /tmp/webserv-cgi-stall.pid
	exec /bin/sleep 60
fi

printf "Content-type: text/html\r\n\r\n"
printf "You sent: %s\n" "$BODY"
