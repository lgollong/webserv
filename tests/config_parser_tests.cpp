#include "Config.hpp"
#include <cstdio>
#include <fcntl.h>
#include <iostream>
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
	path << "/private/tmp/webserv-config-parser-" << getpid() << "-" << label << ".conf";
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

	expectConfigError(temporaryPath("missing"), "unable to open config file",
		"rejects unreadable configuration files");

	const std::string quotePath = temporaryPath("quote");
	expect(writeFile(quotePath, "server { server_name \"unterminated; }\n"),
		"write unclosed quote fixture");
	expectConfigError(quotePath, "unclosed quote", "reports unclosed quotes");
	std::remove(quotePath.c_str());

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

	const std::string emptyPath = temporaryPath("empty");
	expect(writeFile(emptyPath, "# no servers\n"), "write empty parser fixture");
	expectConfigError(emptyPath, "no server blocks found", "rejects configurations without servers");
	std::remove(emptyPath.c_str());

	if (g_failures != 0)
		return 1;
	std::cout << "config parser tests passed" << std::endl;
	return 0;
}
