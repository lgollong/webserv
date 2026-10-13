#!/usr/bin/env python3
"""CGI demo for webserv: echoes the request it receives."""
import html
import os
import sys

method = os.environ.get("REQUEST_METHOD", "")
query = os.environ.get("QUERY_STRING", "")
length = int(os.environ.get("CONTENT_LENGTH") or 0)
body = sys.stdin.buffer.read(length).decode("utf-8", "replace") if length > 0 else ""

if query == "context":
    sys.stdout.write("Content-Type: text/plain\r\n\r\n")
    for name in ("REQUEST_METHOD", "SCRIPT_NAME", "SCRIPT_FILENAME", "PATH_INFO",
                 "QUERY_STRING", "REQUEST_URI", "REMOTE_ADDR", "REDIRECT_STATUS",
                 "SERVER_PROTOCOL", "CONTENT_LENGTH", "CONTENT_TYPE"):
        sys.stdout.write("%s=%s\n" % (name, os.environ.get(name, "")))
    sys.stdout.write("BODY=%s\n" % body)
    with open("context.txt") as relative:
        sys.stdout.write("RELATIVE_FILE=" + relative.read())
    sys.exit(0)

sys.stdout.write("Content-Type: text/html\r\n\r\n")
sys.stdout.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Python CGI</title></head><body>")
sys.stdout.write("<h1>Hello from Python CGI</h1><ul>")
sys.stdout.write("<li>Method: %s</li>" % html.escape(method))
sys.stdout.write("<li>Query string: %s</li>" % html.escape(query))
sys.stdout.write("<li>Body: %s</li>" % html.escape(body))
sys.stdout.write("</ul>")
sys.stdout.write("<form method=\"post\" action=\"%s\"><input name=\"message\" value=\"hi\"> "
                 "<button>POST to this script</button></form>" % html.escape(os.environ.get("SCRIPT_NAME", "")))
sys.stdout.write("</body></html>\n")
