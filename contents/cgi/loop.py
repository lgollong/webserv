#!/usr/bin/env python3
"""CGI error fixture: never finishes. webserv stops it after its 15 s CGI timeout and answers 502."""
while True:
    pass
