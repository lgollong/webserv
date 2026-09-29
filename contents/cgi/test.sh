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
if [ "$QUERY_STRING" = "context" ]; then
	printf "REQUEST_METHOD=%s\n" "$REQUEST_METHOD"
	printf "SCRIPT_NAME=%s\n" "$SCRIPT_NAME"
	printf "PATH_INFO=%s\n" "$PATH_INFO"
	printf "QUERY_STRING=%s\n" "$QUERY_STRING"
	printf "SERVER_NAME=%s\n" "$SERVER_NAME"
	printf "SERVER_PORT=%s\n" "$SERVER_PORT"
	printf "SERVER_PROTOCOL=%s\n" "$SERVER_PROTOCOL"
	printf "CONTENT_LENGTH=%s\n" "$CONTENT_LENGTH"
	printf "CONTENT_TYPE=%s\n" "$CONTENT_TYPE"
	printf "HTTP_X_CGI_TEST=%s\n" "$HTTP_X_CGI_TEST"
	printf "BODY=%s\n" "$BODY"
	printf "RELATIVE_FILE="
	cat context.txt
	exit 0
fi

if [ "$QUERY_STRING" = "large" ]; then
	i=0
	while [ "$i" -lt 20000 ]; do
		printf "x"
		i=$((i + 1))
	done
	exit 0
fi

printf "You sent: %s\n" "$BODY"
