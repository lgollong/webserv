#include "Config.hpp"
#include "utils.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

typedef std::vector<std::string> Tokens;

static const char kDefaultRoot[] = "./contents";

static void fail(const std::string &message) {
	throw std::runtime_error("config: " + message);
}

static Tokens tokenize(const std::string &configPath) {
	std::ifstream input(configPath.c_str());
	if (!input)
		fail("unable to open " + configPath);
	std::ostringstream buffer;
	buffer << input.rdbuf();
	const std::string source = buffer.str();

	Tokens tokens;
	std::string current;
	for (std::string::size_type i = 0; i <= source.size(); ++i) {
		char c = ' ';
		if (i < source.size())
			c = source[i];
		if (c == '#') {
			while (i < source.size() && source[i] != '\n')
				++i;
			c = ' ';
		}
		if (std::isspace(static_cast<unsigned char>(c)) || c == ';' || c == '{' || c == '}') {
			if (!current.empty())
				tokens.push_back(current);
			current.clear();
			if (c == ';' || c == '{' || c == '}')
				tokens.push_back(std::string(1, c));
		}
		else
			current += c;
	}
	return tokens;
}

static void expect(const Tokens &tokens, size_t &i, const std::string &expected) {
	if (i >= tokens.size() || tokens[i] != expected) {
		std::string found = "end of file";
		if (i < tokens.size())
			found = tokens[i];
		fail("expected '" + expected + "', got '" + found + "'");
	}
	++i;
}

static Tokens arguments(const Tokens &tokens, size_t &i, const std::string &directive) {
	Tokens args;
	while (i < tokens.size() && tokens[i] != ";" && tokens[i] != "{" && tokens[i] != "}")
		args.push_back(tokens[i++]);
	if (args.empty())
		fail("'" + directive + "' needs a value");
	expect(tokens, i, ";");
	return args;
}

static size_t parseNumber(const std::string &value, size_t min, size_t max) {
	if (value.empty() || value.size() > 9 || value.find_first_not_of("0123456789") != std::string::npos)
		fail("invalid number '" + value + "'");
	size_t number = std::strtoul(value.c_str(), NULL, 10);
	if (number < min || number > max)
		fail("number out of range '" + value + "'");
	return number;
}

static size_t parseBodySize(std::string value) {
	size_t multiplier = 1;
	char suffix = '\0';
	if (!value.empty())
		suffix = static_cast<char>(std::tolower(value[value.size() - 1]));
	if (suffix == 'k' || suffix == 'm') {
		if (suffix == 'k')
			multiplier = 1024;
		else
			multiplier = 1024 * 1024;
		value.erase(value.size() - 1);
	}
	return parseNumber(value, 0, 999999999) * multiplier;
}

static bool parseSwitch(const Tokens &args, const std::string &directive) {
	if (args.size() != 1 || (args[0] != "on" && args[0] != "off"))
		fail("'" + directive + "' expects on or off");
	return args[0] == "on";
}

static void parseLocation(ServerConfig &server, const Tokens &tokens, size_t &i) {
	Route route;
	if (i >= tokens.size() || tokens[i] == "{")
		fail("location needs a path");
	route.location = tokens[i++];
	expect(tokens, i, "{");

	while (i < tokens.size() && tokens[i] != "}") {
		const std::string directive = tokens[i++];
		Tokens args = arguments(tokens, i, directive);
		if (directive == "root" && args.size() == 1)
			route.root = args[0];
		else if (directive == "index" && args.size() == 1)
			route.index_file = args[0];
		else if (directive == "upload_store" && args.size() == 1)
			route.upload_store = args[0];
		else if (directive == "autoindex")
			route.autoindex = parseSwitch(args, directive);
		else if (directive == "upload")
			route.upload_enabled = parseSwitch(args, directive);
		else if (directive == "session_demo")
			route.session_demo = parseSwitch(args, directive);
		else if (directive == "allowed_methods")
			route.allowed_methods.insert(args.begin(), args.end());
		else if (directive == "return" && args.size() <= 2) {
			route.redirect_status = 302;
			if (args.size() == 2)
				route.redirect_status = static_cast<int>(parseNumber(args[0], 300, 399));
			route.redirect_target = args.back();
		}
		else if (directive == "cgi" && args.size() == 2 && args[1][0] == '/')
			route.cgi_handlers[args[0]] = args[1];
		else
			fail("invalid directive '" + directive + "' in location " + route.location);
	}
	expect(tokens, i, "}");

	if (route.allowed_methods.empty())
		route.allowed_methods.insert("GET");
	if (route.upload_enabled && route.upload_store.empty())
		fail("upload on requires upload_store in location " + route.location);
	server.locations.push_back(route);
}

static ServerConfig parseServer(const Tokens &tokens, size_t &i) {
	ServerConfig server;
	server.host = "0.0.0.0";
	server.port = 8080;
	server.client_max_body_size = 1000000;
	expect(tokens, i, "{");

	while (i < tokens.size() && tokens[i] != "}") {
		const std::string directive = tokens[i++];
		if (directive == "location") {
			parseLocation(server, tokens, i);
			continue;
		}
		Tokens args = arguments(tokens, i, directive);
		if (directive == "listen" && args.size() == 1)
			server.port = static_cast<int>(parseNumber(args[0], 1, 65535));
		else if (directive == "host" && args.size() == 1)
			server.host = args[0];
		else if (directive == "server_name" && args.size() == 1)
			server.server_name = args[0];
		else if (directive == "root" && args.size() == 1)
			server.root = args[0];
		else if (directive == "client_max_body_size" && args.size() == 1)
			server.client_max_body_size = parseBodySize(args[0]);
		else if (directive == "error_page" && args.size() == 2)
			server.error_pages[static_cast<int>(parseNumber(args[0], 400, 599))] = args[1];
		else
			fail("invalid directive '" + directive + "' in server");
	}
	expect(tokens, i, "}");

	if (server.root.empty())
		server.root = kDefaultRoot;
	if (server.locations.empty()) {
		Route fallback;
		fallback.location = "/";
		fallback.allowed_methods.insert("GET");
		server.locations.push_back(fallback);
	}
	for (std::vector<Route>::iterator it = server.locations.begin(); it != server.locations.end(); ++it) {
		if (it->root.empty())
			it->root = server.root;
	}
	return server;
}

Config::Config(const std::string &configPath) {
	Tokens tokens = tokenize(configPath);
	size_t i = 0;
	while (i < tokens.size()) {
		if (tokens[i++] != "server")
			fail("unexpected token '" + tokens[i - 1] + "'");
		server_configs.push_back(parseServer(tokens, i));
	}
	if (server_configs.empty())
		fail("no server blocks in " + configPath);
}

Config::~Config() {}

const std::vector<ServerConfig> &Config::servers() const {
	return server_configs;
}

std::string Config::errorPage(size_t serverIndex, int status) const {
	const std::map<int, std::string> &pages = server_configs[serverIndex].error_pages;
	std::map<int, std::string>::const_iterator it = pages.find(status);
	if (it == pages.end())
		return "";
	return it->second;
}

size_t Config::bodyLimit(size_t serverIndex) const {
	return server_configs[serverIndex].client_max_body_size;
}

static void resolveCgi(const std::string &path, Route &route) {
	for (std::string::size_type end = path.find('/', 1); ; end = path.find('/', end + 1)) {
		const std::string script = path.substr(0, end);
		std::string::size_type dot = script.find_last_of('.');
		std::map<std::string, std::string>::const_iterator handler = route.cgi_handlers.end();
		if (dot != std::string::npos && dot > script.find_last_of('/'))
			handler = route.cgi_handlers.find(script.substr(dot));
		if (handler != route.cgi_handlers.end()) {
			std::string relative = script.substr(route.location.size());
			while (!relative.empty() && relative[0] == '/')
				relative.erase(0, 1);
			if (relative.empty() || hasDotSegment(relative))
				return;
			route.is_cgi = true;
			route.cgi_handler = handler->second;
			route.cgi_script_name = script;
			route.cgi_script_path = route.root;
			if (route.root[route.root.size() - 1] != '/')
				route.cgi_script_path += "/";
			route.cgi_script_path += relative;
			return;
		}
		if (end == std::string::npos)
			return;
	}
}

Route Config::route(size_t serverIndex, const Request &request) const {
	const std::vector<Route> &locations = server_configs[serverIndex].locations;
	const Route *best = NULL;
	for (std::vector<Route>::const_iterator it = locations.begin(); it != locations.end(); ++it) {
		if (matchesLocation(request.path, it->location) && (best == NULL || it->location.size() > best->location.size()))
			best = &*it;
	}
	Route result;
	if (best == NULL) {
		result.location = "/";
		result.root = server_configs[serverIndex].root;
		result.allowed_methods.insert("GET");
		return result;
	}
	result = *best;
	if (!result.cgi_handlers.empty())
		resolveCgi(request.path, result);
	return result;
}
