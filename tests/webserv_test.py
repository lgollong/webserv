#!/usr/bin/env python3
"""Integration test for webserv: starts ./webserv with config/default.config and
walks the evaluation sheet over real sockets. Run from the repository root:

    python3 tests/webserv_test.py          # every check, including the 15 s CGI timeout
    python3 tests/webserv_test.py --fast   # skip the CGI timeout check
"""
import os
import socket
import subprocess
import sys
import threading
import time

HOST = "127.0.0.1"
MAIN = 8080
SECOND = 8081
UPLOADS = "contents/uploads"

failures = []


def check(name, condition, detail=""):
    print(("ok    " if condition else "FAIL  ") + name + ("" if condition else "  " + detail))
    if not condition:
        failures.append(name)


def raw(data, port=MAIN, timeout=20, until_close=False):
    """Sends raw bytes, returns the first complete response (or everything until close)."""
    sock = socket.create_connection((HOST, port), timeout=timeout)
    sock.sendall(data)
    chunks = []
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            if not until_close and response_complete(b"".join(chunks)):
                break
    except socket.timeout:
        pass
    sock.close()
    return b"".join(chunks)


def response_complete(data):
    end = data.find(b"\r\n\r\n")
    if end < 0:
        return False
    length = 0
    for line in data[:end].split(b"\r\n")[1:]:
        name, _, value = line.partition(b":")
        if name.strip().lower() == b"content-length":
            length = int(value.strip())
    return len(data) >= end + 4 + length


def parse(data):
    head, _, body = data.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    status = int(lines[0].split(" ")[1]) if lines[0].startswith("HTTP/") else 0
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()
    return status, headers, body


def request(method, path, body=b"", port=MAIN, headers=None):
    lines = ["%s %s HTTP/1.1" % (method, path), "Host: localhost", "Connection: close"]
    for name, value in (headers or {}).items():
        lines.append("%s: %s" % (name, value))
    if body or method == "POST":
        lines.append("Content-Length: %d" % len(body))
    return parse(raw(("\r\n".join(lines) + "\r\n\r\n").encode() + body, port))


def multipart(files):
    boundary = "----webservtestboundary"
    parts = []
    for name, data in files:
        parts.append(("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
                      "Content-Type: text/plain\r\n\r\n" % (boundary, name)).encode() + data + b"\r\n")
    body = b"".join(parts) + ("--%s--\r\n" % boundary).encode()
    return body, "multipart/form-data; boundary=" + boundary


def remove_upload(name):
    try:
        os.remove(os.path.join(UPLOADS, name))
    except OSError:
        pass


def test_config_and_basics():
    status, _, body = request("GET", "/")
    check("GET / serves the index file", status == 200 and b"<html" in body.lower())
    status, _, body = request("GET", "/does-not-exist")
    check("missing file answers the configured 404 page", status == 404 and body == open("contents/errors/404.html", "rb").read())
    status, _, body = request("GET", "/does-not-exist", port=SECOND)
    check("second server has its own 404 page", status == 404 and body == open("contents/secondary/errors/404.html", "rb").read())
    status, _, body = request("GET", "/", port=SECOND)
    check("second server serves different content", status == 200 and body == open("contents/secondary/index.html", "rb").read())
    check("directory without index or autoindex is 403", request("GET", "/no-index/")[0] == 403)
    status, _, body = request("GET", "/files/")
    check("autoindex lists a directory", status == 200 and b"sample.txt" in body)
    status, _, body = request("GET", "/gallery/")
    check("autoindex escapes names", status == 403 or b"ampersand&amp;entry.txt" in body)
    status, headers, body = request("GET", "/files/sample.txt")
    check("route maps to another directory", status == 200 and body == open("contents/files/sample.txt", "rb").read()
          and headers.get("content-type") == "text/plain")
    status, headers, _ = request("GET", "/old-page")
    check("redirect answers 301 with Location", status == 301 and headers.get("location") == "/")
    status, headers, _ = request("DELETE", "/files/sample.txt")
    check("DELETE without permission is 405", status == 405 and "GET" in headers.get("allow", ""))
    check("POST on a GET-only route is 405", request("POST", "/", b"x")[0] == 405)
    check("unknown method is 501", request("FOO", "/")[0] == 501)
    check("PUT is 501", request("PUT", "/")[0] == 501)
    check("path traversal is rejected", request("GET", "/files/../../Makefile")[0] in (403, 404))


def test_parser():
    check("malformed request line is 400", parse(raw(b"GARBAGE\r\n\r\n"))[0] == 400)
    check("HTTP/2.0 is 505", parse(raw(b"GET / HTTP/2.0\r\nHost: x\r\n\r\n"))[0] == 505)
    check("HTTP/1.0 is answered", parse(raw(b"GET / HTTP/1.0\r\n\r\n"))[0] == 200)
    check("bad Content-Length is 400", parse(raw(b"POST /cgi/test.sh HTTP/1.1\r\nContent-Length: abc\r\n\r\n"))[0] == 400)
    unsupported_encoding = (b"POST /cgi/test.sh HTTP/1.1\r\nHost: x\r\n"
                            b"Transfer-Encoding: gzip\r\n\r\nabc")
    check("unsupported Transfer-Encoding is 400", parse(raw(unsupported_encoding))[0] == 400)
    conflicting_framing = (b"POST /cgi/test.sh HTTP/1.1\r\nHost: x\r\n"
                           b"Transfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n")
    check("Transfer-Encoding with Content-Length is 400", parse(raw(conflicting_framing))[0] == 400)
    repeated_encoding = (b"POST /cgi/test.sh HTTP/1.1\r\nHost: x\r\n"
                         b"Transfer-Encoding: chunked\r\nTransfer-Encoding: gzip\r\n\r\n0\r\n\r\n")
    check("duplicate Transfer-Encoding is 400", parse(raw(repeated_encoding))[0] == 400)
    huge = b"GET / HTTP/1.1\r\nX-Big: " + b"a" * 20000 + b"\r\n\r\n"
    check("oversized headers are rejected", parse(raw(huge))[0] == 431)
    status, _, body = parse(raw(b"POST /cgi/test.sh HTTP/1.1\r\nHost: x\r\nConnection: close\r\n"
                                b"Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n"))
    check("chunked body is de-chunked for CGI", status == 200 and b"You sent: hello world" in body)
    status, _, body = parse(raw(b"POST /cgi/test.sh HTTP/1.1\r\nHost: x\r\nConnection: close\r\n"
                                b"Transfer-Encoding: chunked\r\n\r\n3;ext=1\r\nabc\r\n0\r\nX-Trailer: y\r\n\r\n"))
    check("chunk extensions and trailers are skipped", status == 200 and b"You sent: abc" in body)


def test_body_limit():
    status, _, _ = request("POST", "/cgi/test.sh", b"short", port=SECOND)
    check("body under the limit is accepted", status == 200)
    status, _, _ = request("POST", "/cgi/test.sh", b"BODY IS HERE write something shorter or longer than body limit", port=SECOND)
    check("body over the limit is 413", status == 413)
    chunked = b"POST /cgi/test.sh HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n40\r\n" + b"x" * 64 + b"\r\n0\r\n\r\n"
    check("chunked body over the limit is 413", parse(raw(chunked, SECOND))[0] == 413)


def test_uploads():
    name = "webserv-test-raw.txt"
    remove_upload(name)
    status, headers, _ = request("POST", "/uploads/" + name, b"raw upload body")
    check("raw POST upload is 201 with Location", status == 201 and headers.get("location") == "/uploads/" + name)
    status, _, body = request("GET", "/uploads/" + name)
    check("uploaded file can be retrieved", status == 200 and body == b"raw upload body")
    check("uploading an existing name is 409", request("POST", "/uploads/" + name, b"again")[0] == 409)
    check("DELETE removes the file", request("DELETE", "/uploads/" + name)[0] == 204)
    check("deleted file is 404", request("GET", "/uploads/" + name)[0] == 404)
    check("DELETE of a missing file is 404", request("DELETE", "/uploads/" + name)[0] == 404)

    names = ["webserv-test-a.txt", "webserv-test-b.bin"]
    for each in names:
        remove_upload(each)
    payload = bytes(range(256)) * 4
    body, content_type = multipart([(names[0], b"first file"), (names[1], payload)])
    status, _, _ = request("POST", "/uploads", body, headers={"Content-Type": content_type})
    check("multipart upload with two files is 201", status == 201)
    check("multipart text file is stored", request("GET", "/uploads/" + names[0])[2] == b"first file")
    check("multipart binary file is stored byte for byte", request("GET", "/uploads/" + names[1])[2] == payload)
    for each in names:
        request("DELETE", "/uploads/" + each)
    body, content_type = multipart([("../../escape.txt", b"x")])
    request("POST", "/uploads", body, headers={"Content-Type": content_type})
    check("multipart file names cannot leave the upload store", not os.path.exists("escape.txt") and not os.path.exists("contents/escape.txt"))
    remove_upload("escape.txt")


def test_cgi(fast):
    status, _, body = request("GET", "/cgi/test.py?name=evaluator")
    check("python CGI GET", status == 200 and b"Hello from Python CGI" in body and b"name=evaluator" in body)
    status, _, body = request("POST", "/cgi/test.sh", b"posted to a shell script")
    check("shell CGI POST receives the body", status == 200 and b"You sent: posted to a shell script" in body)
    status, _, body = request("POST", "/cgi/test.py/extra/path?context", b"context check", headers={"Content-Type": "text/plain"})
    check("CGI gets environment and runs in its directory",
          status == 200 and b"REQUEST_METHOD=POST" in body and b"PATH_INFO=/extra/path" in body
          and b"QUERY_STRING=context" in body and b"BODY=context check" in body and b"RELATIVE_FILE=" in body
          and b"CONTENT_TYPE=text/plain" in body)
    status, _, body = request("GET", "/cgi/test.sh?context", headers={"X-Cgi-Test": "yes"})
    check("request headers reach the CGI as HTTP_*", status == 200 and b"HTTP_X_CGI_TEST=yes" in body)
    check("crashing CGI is 502", request("GET", "/cgi/crash.py")[0] == 502)
    check("CGI without header block is 502", request("GET", "/cgi/test.sh?malformed")[0] == 502)
    check("missing CGI script is 502", request("GET", "/cgi/missing.py")[0] == 502)
    if not fast:
        started = time.time()
        status = request("GET", "/cgi/loop.py", port=MAIN)[0]
        check("endless CGI is stopped with 502", status == 502 and time.time() - started < 25)
        check("server answers after the CGI timeout", request("GET", "/")[0] == 200)


def test_session():
    status, headers, body = request("GET", "/session")
    cookie = headers.get("set-cookie", "").split(";")[0]
    check("first visit sets a session cookie", status == 200 and cookie and b"visits: 1" in body)
    status, headers, body = request("GET", "/session", headers={"Cookie": cookie})
    check("second visit is counted in the session", status == 200 and b"visits: 2" in body and "set-cookie" not in headers)
    status, headers, body = request("GET", "/session", headers={"Cookie": "webserv_session=bogus"})
    check("unknown session starts a new one", status == 200 and b"visits: 1" in body and "set-cookie" in headers)


def test_connections():
    sock = socket.create_connection((HOST, MAIN), timeout=5)
    sock.sendall(b"GET /files/sample.txt HTTP/1.1\r\nHost: x\r\n\r\n")
    first = b""
    while not response_complete(first):
        first += sock.recv(65536)
    sock.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    second = b""
    while True:
        chunk = sock.recv(65536)
        if not chunk:
            break
        second += chunk
    sock.close()
    check("keep-alive serves two requests on one connection", parse(first)[0] == 200 and parse(second)[0] == 200)

    pipelined = raw(b"GET /files/sample.txt HTTP/1.1\r\nHost: x\r\n\r\nGET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", until_close=True)
    check("pipelined requests are both answered", pipelined.count(b"HTTP/1.1 200") == 2)

    for _ in range(20):
        sock = socket.create_connection((HOST, MAIN))
        sock.sendall(b"POST /cgi/test.sh HTTP/1.1\r\nContent-Length: 100\r\n\r\npartial")
        sock.close()
    check("clients that disconnect mid-request do not hurt the server", request("GET", "/")[0] == 200)

    results = []

    def client():
        try:
            results.append(request("GET", "/empty.html")[0])
        except OSError:
            results.append(0)

    threads = [threading.Thread(target=client) for _ in range(200)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    check("200 concurrent clients are all served", results.count(200) == 200, str(sorted(set(results))))

    big = os.urandom(3 * 1024 * 1024)
    name = "webserv-test-big.bin"
    remove_upload(name)
    request("POST", "/uploads/" + name, big)
    status, _, body = request("GET", "/uploads/" + name)
    check("large file round-trips", status == 200 and body == big)
    request("DELETE", "/uploads/" + name)


def test_port_conflict():
    result = subprocess.run(["./webserv", "config/conflict.config"], capture_output=True, timeout=5)
    check("same interface:port twice is a startup error", result.returncode != 0 and b"Address already in use" in result.stderr)


def wait_for_port(port):
    for _ in range(50):
        try:
            socket.create_connection((HOST, port), timeout=1).close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def main():
    fast = "--fast" in sys.argv
    server = subprocess.Popen(["./webserv", "config/default.config"])
    try:
        if not wait_for_port(MAIN) or not wait_for_port(SECOND):
            print("webserv did not start")
            return 1
        test_config_and_basics()
        test_parser()
        test_body_limit()
        test_uploads()
        test_cgi(fast)
        test_session()
        test_connections()
        test_port_conflict()
        check("server is still running", server.poll() is None)
    finally:
        server.terminate()
        server.wait()
    print("\n%d failure(s)" % len(failures) if failures else "\nall checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
