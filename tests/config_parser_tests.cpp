#include "Config.hpp"
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

static int g_failures = 0;

static void expect(bool condition, const char *message) {
	if (condition)
		return;
	std::cerr << "failure: " << message << std::endl;
	++g_failures;
}

static std::string temporaryPath(const char *label) {
	std::ostringstream path;
	path << "/tmp/webserv-config-parser-" << getpid() << "-" << label << ".conf";
	return path.str();
}

static bool writeFile(const std::string &path, const std::string &contents) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return false;

	std::string::size_type written = 0;
	while (written < contents.size()) {
		ssize_t count = write(fd, contents.data() + written, contents.size() - written);
		if (count <= 0) {
			close(fd);
			std::remove(path.c_str());
			return false;
		}
		written += static_cast<std::string::size_type>(count);
	}
	return close(fd) == 0;
}

static void expectConfigError(const std::string &path, const std::string &expected,
	const char *message) {
	try {
		Config config(path);
		(void)config;
		expect(false, message);
	} catch (const std::exception &error) {
		expect(std::string(error.what()).find(expected) != std::string::npos, message);
	}
}

static Request requestFor(const std::string &path) {
	Request request;
	request.path = path;
	return request;
}

static std::string bodySizeConfig(const std::string &value) {
	return "server { client_max_body_size " + value + "; }\n";
}

static void expectBodyLimit(const std::string &value, size_t expected, const char *message) {
	const std::string path = temporaryPath("body-size");
	if (!writeFile(path, bodySizeConfig(value))) {
		expect(false, message);
		return;
	}
	try {
		Config config(path);
		expect(config.bodyLimit(0) == expected, message);
	} catch (const std::exception &error) {
		std::cerr << "failure: body size fixture threw: " << error.what() << std::endl;
		++g_failures;
	}
	std::remove(path.c_str());
}

static void expectBodySizeError(const std::string &value, const std::string &expected,
	const char *message) {
	const std::string path = temporaryPath("invalid-body-size");
	if (!writeFile(path, bodySizeConfig(value))) {
		expect(false, message);
		return;
	}
	expectConfigError(path, expected, message);
	std::remove(path.c_str());
}

int main() {
	const std::string validPath = temporaryPath("valid");
	const std::string validConfig =
		"# Multiple servers and locations are supported.\n"
		"server {\n"
		"  host 127.0.0.1;\n"
		"  listen 9001;\n"
		"  server_name \"one.example\";\n"
		"  location / {\n"
		"    allowed_methods GET POST;\n"
		"  }\n"
		"  location /assets {\n"
		"    root ./contents/files;\n"
		"    autoindex on;\n"
		"  }\n"
		"  root './contents';\n"
		"}\n"
		"server {\n"
		"  host 127.0.0.1;\n"
		"  listen 9002;\n"
		"  location /api { allowed_methods GET; }\n"
		"  location /cgi { cgi .sh /bin/sh; }\n"
		"}\n";

	expect(writeFile(validPath, validConfig), "write valid parser fixture");
	try {
		Config config(validPath);
		const std::vector<ServerConfig> &servers = config.servers();
		expect(servers.size() == 2, "parses multiple server blocks");
		if (servers.size() == 2) {
			expect(servers[0].host == "127.0.0.1" && servers[0].port == 9001 &&
				servers[0].server_name == "one.example", "parses first server directives");
			expect(servers[0].locations.size() == 2 && servers[1].locations.size() == 2,
				"parses multiple locations per server");
			if (servers[0].locations.size() == 2) {
				expect(servers[0].locations[1].location == "/assets" &&
					servers[0].locations[1].root == "./contents/files" &&
					servers[0].locations[1].autoindex, "parses quoted values and location directives");
				expect(config.route(0, requestFor("/page.html")).root == "./contents",
					"inherits a server root declared after the location");
				expect(config.route(0, requestFor("/assets/page.html")).root == "./contents/files",
					"keeps an explicit location root over the server root");
			}
			if (servers[1].locations.size() == 2) {
				std::map<std::string, std::string>::const_iterator cgi =
					servers[1].locations[1].cgi_handlers.find(".sh");
				expect(cgi != servers[1].locations[1].cgi_handlers.end() && cgi->second == "/bin/sh",
					"parses CGI handler in second server");
			}
		}
	} catch (const std::exception &error) {
		std::cerr << "failure: valid parser fixture threw: " << error.what() << std::endl;
		++g_failures;
	}
	std::remove(validPath.c_str());

	const std::string defaultsPath = temporaryPath("defaults");
	expect(writeFile(defaultsPath, "server { }\n"), "write default parser fixture");
	try {
		Config config(defaultsPath);
		const ServerConfig &server = config.servers()[0];
		expect(server.host == "0.0.0.0" && server.port == 8080, "applies listener defaults");
		expect(server.locations.size() == 1 && server.locations[0].location == "/" &&
			server.locations[0].root == "./contents", "applies fallback location defaults");
	} catch (const std::exception &error) {
		std::cerr << "failure: default parser fixture threw: " << error.what() << std::endl;
		++g_failures;
	}
	std::remove(defaultsPath.c_str());

	const std::string fallbackPath = temporaryPath("server-root-fallback");
	expect(writeFile(fallbackPath, "server { root ./contents/files; }\n"),
		"write server root fallback fixture");
	try {
		Config config(fallbackPath);
		const ServerConfig &server = config.servers()[0];
		expect(server.locations.size() == 1 && server.locations[0].root == "./contents/files",
			"uses the server root for a synthesized fallback location");
	} catch (const std::exception &error) {
		std::cerr << "failure: server root fallback fixture threw: " << error.what() << std::endl;
		++g_failures;
	}
	std::remove(fallbackPath.c_str());

	const std::string uploadPath = temporaryPath("upload-policy");
	expect(writeFile(uploadPath,
		"server {\n"
		"  location /enabled { upload on; upload_store ./contents/uploads; }\n"
		"  location /disabled { upload_store ./contents/uploads; upload off; }\n"
		"}\n"), "write upload policy fixture");
	try {
		Config config(uploadPath);
		expect(config.route(0, requestFor("/enabled/file.txt")).upload_enabled,
			"parses upload on into the resolved route");
		expect(!config.route(0, requestFor("/disabled/file.txt")).upload_enabled,
			"parses upload off into the resolved route");
	} catch (const std::exception &error) {
		std::cerr << "failure: upload policy fixture threw: " << error.what() << std::endl;
		++g_failures;
	}
	std::remove(uploadPath.c_str());

	expectBodyLimit("0", 0, "accepts a zero-byte body limit");
	expectBodyLimit("42", 42, "accepts an unsigned decimal body limit");
	expectBodyLimit("2K", static_cast<size_t>(2 * 1024), "accepts an uppercase kilobyte suffix");
	expectBodyLimit("3m", static_cast<size_t>(3 * 1024 * 1024), "accepts a lowercase megabyte suffix");
	expectBodyLimit("1G", static_cast<size_t>(1024 * 1024 * 1024), "accepts a gigabyte suffix");

	expectBodySizeError("-1", "invalid body size", "rejects negative body sizes");
	expectBodySizeError("+1", "invalid body size", "rejects explicitly signed body sizes");
	expectBodySizeError("1.5", "invalid body size", "rejects decimal body sizes");
	expectBodySizeError("1e3", "invalid body size", "rejects exponent body sizes");
	expectBodySizeError("1kk", "invalid body size", "rejects repeated body-size suffixes");
	expectBodySizeError("10kb", "invalid body size", "rejects multi-character body-size suffixes");
	expectBodySizeError("k", "invalid body size", "requires digits before a body-size suffix");
	expectBodySizeError("\"1 0\"", "invalid body size", "rejects whitespace inside quoted body sizes");
	expectBodySizeError("\" 1\"", "invalid body size", "rejects leading whitespace in quoted body sizes");

	std::ostringstream maximumBodySize;
	maximumBodySize << std::numeric_limits<size_t>::max();
	expectBodySizeError(maximumBodySize.str() + "K", "body size overflow",
		"rejects body sizes that overflow after suffix multiplication");
	expectBodySizeError(maximumBodySize.str() + "0", "body size overflow",
		"rejects body sizes that overflow during decimal conversion");

	expectConfigError(temporaryPath("missing"), "unable to open config file",
		"rejects unreadable configuration files");

	const std::string quotePath = temporaryPath("quote");
	expect(writeFile(quotePath, "server { server_name \"unterminated; }\n"),
		"write unclosed quote fixture");
	expectConfigError(quotePath, "unclosed quote", "reports unclosed quotes");
	std::remove(quotePath.c_str());

	const std::string quoteLinePath = temporaryPath("quote-line");
	expect(writeFile(quoteLinePath,
		"server {\n\tlisten 9001;\n\troot \"/var/www;\n\tserver_name x;\n}\n\n"),
		"write multi-line unclosed quote fixture");
	expectConfigError(quoteLinePath, ":3: unclosed quote",
		"reports the line where an unclosed quote was opened");
	std::remove(quoteLinePath.c_str());

	const std::string lineEndPath = temporaryPath("line-end");
	expect(writeFile(lineEndPath, "server {\n\tlisten 9001;\n\tbogus\n\tfoo;\n}\n"),
		"write directive-at-line-end fixture");
	expectConfigError(lineEndPath, ":3: unknown directive 'bogus'",
		"reports the line of a token that ends at a newline");
	std::remove(lineEndPath.c_str());

	const std::string commentSplitPath = temporaryPath("comment-split");
	expect(writeFile(commentSplitPath, "server {\n\tbogus# trailing comment\n\tfoo;\n}\n"),
		"write token-before-comment fixture");
	expectConfigError(commentSplitPath, ":2: unknown directive 'bogus'",
		"ends a token at an inline comment");
	std::remove(commentSplitPath.c_str());

	const std::string semicolonPath = temporaryPath("semicolon");
	expect(writeFile(semicolonPath, "server { listen 9001 location / { root ./contents; } }\n"),
		"write missing semicolon fixture");
	expectConfigError(semicolonPath, "expected ';'", "reports missing semicolons");
	std::remove(semicolonPath.c_str());

	const std::string directivePath = temporaryPath("directive");
	expect(writeFile(directivePath, "server { unexpected value; }\n"),
		"write unknown directive fixture");
	expectConfigError(directivePath, "unknown directive 'unexpected'", "reports unknown directives");
	std::remove(directivePath.c_str());

	const std::string portPath = temporaryPath("port");
	expect(writeFile(portPath, "server { listen 0; }\n"), "write invalid port fixture");
	expectConfigError(portPath, "port out of range", "rejects invalid ports");
	std::remove(portPath.c_str());

	const std::string uploadErrorPath = temporaryPath("upload-missing-store");
	expect(writeFile(uploadErrorPath, "server { location /uploads { upload on; } }\n"),
		"write upload without store fixture");
	expectConfigError(uploadErrorPath, "upload on requires upload_store",
		"rejects enabled uploads without a storage directory");
	std::remove(uploadErrorPath.c_str());

	const std::string defaultLogPath = temporaryPath("log-level-default");
	expect(writeFile(defaultLogPath, "server { listen 8080; }\n"), "write default log level fixture");
	{
		Config config(defaultLogPath);
		expect(config.logLevel() == LOG_ERROR, "log level defaults to error");
	}
	std::remove(defaultLogPath.c_str());

	const char *const logValues[] = {"none", "ERROR", "debug"};
	const LogLevel logLevels[] = {LOG_NONE, LOG_ERROR, LOG_DEBUG};
	for (size_t i = 0; i < 3; ++i) {
		const std::string logPath = temporaryPath("log-level");
		expect(writeFile(logPath, std::string("log_level ") + logValues[i] + ";\nserver { listen 8080; }\n"),
			"write log level fixture");
		Config config(logPath);
		expect(config.logLevel() == logLevels[i], "parses top-level log_level values case-insensitively");
		std::remove(logPath.c_str());
	}

	const std::string invalidLogPath = temporaryPath("log-level-invalid");
	expect(writeFile(invalidLogPath, "server { listen 8080; }\nlog_level verbose;\n"), "write invalid log level fixture");
	expectConfigError(invalidLogPath, ":2: invalid log_level 'verbose'", "rejects unknown log levels with their line");
	std::remove(invalidLogPath.c_str());

	const std::string duplicateLogPath = temporaryPath("log-level-duplicate");
	expect(writeFile(duplicateLogPath, "log_level none;\nlog_level debug;\nserver { listen 8080; }\n"),
		"write duplicate log level fixture");
	expectConfigError(duplicateLogPath, ":2: duplicate log_level", "rejects a repeated log_level");
	std::remove(duplicateLogPath.c_str());

	const std::string missingLogPath = temporaryPath("log-level-missing");
	expect(writeFile(missingLogPath, "log_level;\nserver { listen 8080; }\n"), "write missing log level fixture");
	expectConfigError(missingLogPath, "log_level missing value", "rejects log_level without a value");
	std::remove(missingLogPath.c_str());

	const std::string cgiRootPath = temporaryPath("cgi-root");
	expect(writeFile(cgiRootPath, "server {\n listen 8080;\n location /scripts {\n  root /var/www/cgi-bin;\n"
		"  cgi .py /usr/bin/python3;\n }\n location / {\n  root ./contents;\n  cgi .sh /bin/bash;\n }\n}\n"),
		"write CGI root fixture");
	{
		Config config(cgiRootPath);
		Route direct = config.route(0, requestFor("/scripts/test.py"));
		expect(direct.is_cgi && direct.cgi_script_name == "/scripts/test.py" &&
			direct.cgi_script_path == "/var/www/cgi-bin/test.py",
			"CGI script resolves beneath its location root with the location prefix removed");
		Route pathInfo = config.route(0, requestFor("/scripts/nested/test.py/extra/info"));
		expect(pathInfo.is_cgi && pathInfo.cgi_script_name == "/scripts/nested/test.py" &&
			pathInfo.cgi_script_path == "/var/www/cgi-bin/nested/test.py",
			"PATH_INFO stays outside the resolved script path");
		Route rootLocation = config.route(0, requestFor("/cgi/test.sh"));
		expect(rootLocation.is_cgi && rootLocation.cgi_script_path == "./contents/cgi/test.sh",
			"root location keeps the full URL path beneath its root");
		Route traversal = config.route(0, requestFor("/scripts/../test.py"));
		expect(!traversal.is_cgi && traversal.cgi_script_path.empty(),
			"CGI script traversal is rejected lexically");
	}
	std::remove(cgiRootPath.c_str());

	const std::string relativeHandlerPath = temporaryPath("relative-cgi-handler");
	expect(writeFile(relativeHandlerPath, "server {\n location /cgi {\n  cgi .py python3;\n }\n}\n"),
		"write relative CGI handler fixture");
	expectConfigError(relativeHandlerPath, ":3: cgi handler must be an absolute path 'python3'",
		"rejects relative CGI handler paths with their line number");
	std::remove(relativeHandlerPath.c_str());

	const std::string emptyPath = temporaryPath("empty");
	expect(writeFile(emptyPath, "# no servers\n"), "write empty parser fixture");
	expectConfigError(emptyPath, "no server blocks found", "rejects configurations without servers");
	std::remove(emptyPath.c_str());

	if (g_failures != 0)
		return 1;
	std::cout << "config parser tests passed" << std::endl;
	return 0;
}
