#!/usr/bin/env python3
"""CGI error fixture: fails before writing a CGI header, so webserv answers 502."""
raise RuntimeError("intentional CGI failure for the tester")
