#include "Config.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <iostream>

static std::string trim(const std::string &value) {
	std::string::size_type start = 0;
	while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
		++start;
	std::string::size_type end = value.size();
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
		--end;
	return value.substr(start, end - start);
}

static std::string toLowerCopy(const std::string &value) {
	std::string out;
	for (std::string::size_type i = 0; i < value.size(); ++i)
		out += static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
	return out;
}

static bool isBooleanTrue(const std::string &value) {
	std::string lowered = toLowerCopy(value);
	return lowered == "on" || lowered == "true" || lowered == "yes" || lowered == "1";
}

static bool isBooleanFalse(const std::string &value) {
	std::string lowered = toLowerCopy(value);
	return lowered == "off" || lowered == "false" || lowered == "no" || lowered == "0";
}

static std::string formatError(const std::string &path, std::size_t line, const std::string &msg) {
	std::ostringstream oss;
	oss << path << ":" << line << ": " << msg;
	return oss.str();
}

Config::Config(const std::string &configPath) {
	parseFile(configPath);
	if (server_configs.empty())
		throw std::runtime_error(formatError(configPath, 0, "no server blocks found"));
	
	// Set defaults for servers with no listener or locations
	for (std::vector<ServerConfig>::iterator it = server_configs.begin(); it != server_configs.end(); ++it) {
		if (it->host.empty())
			it->host = "0.0.0.0";
		if (it->port == 0)
			it->port = 8080;
		if (it->locations.empty()) {
			Route fallback;
			fallback.location = "/";
			fallback.root = it->root.empty() ? "./contents" : it->root;
			fallback.allowed_methods.insert("GET");
			fallback.redirect_status = 0;
			fallback.autoindex = false;
			it->locations.push_back(fallback);
		}
	}
}

Config::~Config() {}

const std::vector<ServerConfig> &Config::servers() const {
	return server_configs;
}

std::string Config::errorPage(size_t serverIndex, int status) const {
	if (serverIndex >= server_configs.size())
		return "";
	std::map<int, std::string>::const_iterator it = server_configs[serverIndex].error_pages.find(status);
	if (it == server_configs[serverIndex].error_pages.end())
		return "";
	return it->second;
}

size_t Config::bodyLimit(size_t serverIndex) const {
	if (serverIndex >= server_configs.size())
		return 1000000; // 1MB default
	return server_configs[serverIndex].client_max_body_size;
}

std::string Config::readFile(const std::string &configPath) const {
	std::ifstream input(configPath.c_str(), std::ios::in | std::ios::binary);
	if (!input)
		throw std::runtime_error("unable to open config file: " + configPath);
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

std::vector<std::string> Config::tokenize(const std::string &configPath, std::vector<size_t> &lines) const {
	std::string source = readFile(configPath);
	std::vector<std::string> tokens;
	std::vector<size_t> tokenLines;
	std::string current;
	bool in_single = false;
	bool in_double = false;
	size_t line = 1;

	for (std::string::size_type i = 0; i < source.size(); ++i) {
		char c = source[i];
		
		if (c == '\n')
			++line;
		
		if (c == '#' && !in_single && !in_double) {
			while (i < source.size() && source[i] != '\n')
				++i;
			if (i < source.size())
				++line;
			continue;
		}
		
		if (in_single) {
			if (c == '\'') {
				in_single = false;
			} else if (c == '\\' && i + 1 < source.size()) {
				current += source[++i];
				if (source[i] == '\n') ++line;
			} else {
				current += c;
			}
			continue;
		}
		
		if (in_double) {
			if (c == '"') {
				in_double = false;
			} else if (c == '\\' && i + 1 < source.size()) {
				current += source[++i];
				if (source[i] == '\n') ++line;
			} else {
				current += c;
			}
			continue;
		}
		
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!current.empty()) {
				tokens.push_back(current);
				tokenLines.push_back(line);
				current.clear();
			}
			continue;
		}
		
		if (c == ';' || c == '{' || c == '}') {
			if (!current.empty()) {
				tokens.push_back(current);
				tokenLines.push_back(line);
				current.clear();
			}
			tokens.push_back(std::string(1, c));
			tokenLines.push_back(line);
			continue;
		}
		
		if (c == '\\' && i + 1 < source.size() && !in_single && !in_double) {
			current += source[++i];
			if (source[i] == '\n') ++line;
			continue;
		}
		
		if (c == '\'') {
			in_single = true;
			continue;
		}
		
		if (c == '"') {
			in_double = true;
			continue;
		}
		
		current += c;
	}
	
	if (in_single || in_double)
		throw std::runtime_error(formatError(configPath, line, "unclosed quote"));
	
	if (!current.empty()) {
		tokens.push_back(current);
		tokenLines.push_back(line);
	}
	
	lines = tokenLines;
	return tokens;
}

void Config::expectToken(const std::string &expected, std::vector<std::string> &tokens,
	std::size_t &index, const std::string &configPath, const std::vector<size_t> &lines) const {
	if (index >= tokens.size() || tokens[index] != expected) {
		std::size_t line = (index < lines.size()) ? lines[index] : 0;
		throw std::runtime_error(formatError(configPath, line, "expected '" + expected + "', got '" + 
			(index < tokens.size() ? tokens[index] : "EOF") + "'"));
	}
	++index;
}

void Config::expectSemicolon(std::vector<std::string> &tokens, std::size_t &index,
	const std::string &configPath, const std::vector<size_t> &lines) const {
	if (index >= tokens.size() || tokens[index] != ";") {
		std::size_t line = (index < lines.size()) ? lines[index] : 0;
		throw std::runtime_error(formatError(configPath, line, "expected ';'"));
	}
	++index;
}

int Config::parsePort(const std::string &value, const std::string &configPath, std::size_t line) const {
	std::stringstream stream(value);
	int port = 0;
	char extra = 0;
	if (!(stream >> port) || (stream >> extra))
		throw std::runtime_error(formatError(configPath, line, "invalid port '" + value + "'"));
	if (port < 1 || port > 65535)
		throw std::runtime_error(formatError(configPath, line, "port out of range '" + value + "'"));
	return port;
}

size_t Config::parseBodySize(const std::string &value, const std::string &configPath, std::size_t line) const {
	std::string cleaned = trim(value);
	if (cleaned.empty())
		throw std::runtime_error(formatError(configPath, line, "invalid body size"));

	size_t multiplier = 1;
	std::string number = cleaned;
	if (!number.empty()) {
		char last = number[number.size() - 1];
		if (last == 'k' || last == 'K') {
			multiplier = 1024;
			number = number.substr(0, number.size() - 1);
		} else if (last == 'm' || last == 'M') {
			multiplier = 1024 * 1024;
			number = number.substr(0, number.size() - 1);
		} else if (last == 'g' || last == 'G') {
			multiplier = 1024 * 1024 * 1024;
			number = number.substr(0, number.size() - 1);
		}
	}
	if (number.empty())
		throw std::runtime_error(formatError(configPath, line, "invalid body size"));

	std::stringstream stream(number);
	size_t parsed = 0;
	char extra = 0;
	if (!(stream >> parsed) || (stream >> extra))
		throw std::runtime_error(formatError(configPath, line, "invalid body size"));
	if (parsed > std::numeric_limits<size_t>::max() / multiplier)
		throw std::runtime_error(formatError(configPath, line, "body size overflow"));
	return parsed * multiplier;
}

void Config::validateListen(const std::string &host, int port, const std::string &configPath, std::size_t line) const {
	if (host.empty())
		throw std::runtime_error(formatError(configPath, line, "invalid listen address"));
	if (port < 1 || port > 65535)
		throw std::runtime_error(formatError(configPath, line, "port out of range"));
}

std::string Config::parseLocation(ServerConfig &server, std::vector<std::string> &tokens,
	std::size_t &index, const std::string &configPath, std::vector<size_t> &lines) {
	if (index >= tokens.size())
		throw std::runtime_error(formatError(configPath, lines[index - 1], "location missing path"));
	
	std::string location_path = tokens[index++];
	
	if (index >= tokens.size() || tokens[index] != "{")
		throw std::runtime_error(formatError(configPath, lines[index - 1], "expected '{'"));
	++index;

	Route route;
	route.location = location_path;
	route.is_cgi = false;
	route.redirect_status = 0;
	route.autoindex = false;
	route.session_demo = false;
	route.upload_store = "";

	while (index < tokens.size() && tokens[index] != "}") {
		std::string directive = tokens[index];
		std::size_t dir_line = lines[index];
		++index;
		
		if (directive == "root") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "root missing value"));
			route.root = tokens[index++];
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "index") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "index missing value"));
			route.index_file = tokens[index++];
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "autoindex") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "autoindex missing value"));
			std::string value = tokens[index++];
			if (!isBooleanTrue(value) && !isBooleanFalse(value))
				throw std::runtime_error(formatError(configPath, dir_line, "invalid autoindex value"));
			route.autoindex = isBooleanTrue(value);
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "upload_store") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "upload_store missing value"));
			route.upload_store = tokens[index++];
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "upload") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "upload missing value"));
			std::string value = tokens[index++];
			if (!isBooleanTrue(value) && !isBooleanFalse(value))
				throw std::runtime_error(formatError(configPath, dir_line, "invalid upload value"));
			route.upload_enabled = isBooleanTrue(value);
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "return") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "return missing target"));
			
			std::string first = tokens[index++];
			int status = 302;
			std::string target = first;
			
			// Check if first arg is a status code
			std::stringstream ss(first);
			int possible_status = 0;
			if ((ss >> possible_status) && possible_status >= 300 && possible_status <= 399) {
				status = possible_status;
				if (index >= tokens.size())
					throw std::runtime_error(formatError(configPath, dir_line, "return missing target"));
				target = tokens[index++];
			}
			
			route.redirect_status = status;
			route.redirect_target = target;
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "allow_methods" || directive == "allowed_methods") {
			while (index < tokens.size() && tokens[index] != ";") {
				route.allowed_methods.insert(tokens[index++]);
			}
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "cgi") {
			if (index + 1 >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "cgi missing handler"));
			std::string ext = tokens[index++];
			std::string handler = tokens[index++];
			route.cgi_handlers[ext] = handler;
			route.is_cgi = true;
			expectSemicolon(tokens, index, configPath, lines);
		}
		else {
			throw std::runtime_error(formatError(configPath, dir_line, "unknown directive '" + directive + "'"));
		}
	}
	
	if (index >= tokens.size() || tokens[index] != "}")
		throw std::runtime_error(formatError(configPath, lines[index - 1], "expected '}'"));
	++index;

	if (route.allowed_methods.empty())
		route.allowed_methods.insert("GET");
	if (route.upload_enabled && route.upload_store.empty())
		throw std::runtime_error(formatError(configPath, lines[index - 1],
			"upload on requires upload_store"));
	
	server.locations.push_back(route);
	return location_path;
}

void Config::parseServer(std::vector<std::string> &tokens, std::size_t &index,
	const std::string &configPath, std::vector<size_t> &lines) {
	if (index >= tokens.size() || tokens[index] != "server")
		throw std::runtime_error(formatError(configPath, lines[index], "expected 'server'"));
	++index;
	
	if (index >= tokens.size() || tokens[index] != "{")
		throw std::runtime_error(formatError(configPath, lines[index - 1], "expected '{'"));
	++index;

	ServerConfig server;
	server.host = "";
	server.client_max_body_size = 1000000; // default 1MB

	while (index < tokens.size() && tokens[index] != "}") {
		std::string directive = tokens[index];
		std::size_t dir_line = lines[index];
		++index;
		
		if (directive == "listen") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "listen missing value"));
			int port = parsePort(tokens[index++], configPath, dir_line);
			validateListen(server.host.empty() ? "0.0.0.0" : server.host, port, configPath, dir_line);
			server.port = port;
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "host") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "host missing value"));
			server.host = tokens[index++];
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "server_name") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "server_name missing value"));
			server.server_name = tokens[index++];
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "root") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "root missing value"));
			server.root = tokens[index++];
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "client_max_body_size") {
			if (index >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "client_max_body_size missing value"));
			server.client_max_body_size = parseBodySize(tokens[index++], configPath, dir_line);
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "error_page") {
			if (index + 1 >= tokens.size())
				throw std::runtime_error(formatError(configPath, dir_line, "error_page missing arguments"));
			std::string code_text = tokens[index++];
			std::string page = tokens[index++];
			int code = parsePort(code_text, configPath, dir_line);
			if (code < 100 || code > 599)
				throw std::runtime_error(formatError(configPath, dir_line, "invalid error code"));
			server.error_pages[code] = page;
			expectSemicolon(tokens, index, configPath, lines);
		} 
		else if (directive == "location") {
			parseLocation(server, tokens, index, configPath, lines);
		} 
		else {
			throw std::runtime_error(formatError(configPath, dir_line, "unknown directive '" + directive + "'"));
		}
	}
	
	if (index >= tokens.size() || tokens[index] != "}")
		throw std::runtime_error(formatError(configPath, lines[index - 1], "expected '}'"));
	++index;

	const std::string default_root = server.root.empty() ? "./contents" : server.root;
	for (std::vector<Route>::iterator location = server.locations.begin();
		location != server.locations.end(); ++location) {
		if (location->root.empty())
			location->root = default_root;
	}
	
	server_configs.push_back(server);
}

void Config::parseFile(const std::string &configPath) {
	std::vector<size_t> lines;
	std::vector<std::string> tokens = tokenize(configPath, lines);
	std::size_t index = 0;
	
	while (index < tokens.size()) {
		if (tokens[index] == "server") {
			parseServer(tokens, index, configPath, lines);
		} 
		else if (tokens[index] == "}") {
			std::size_t line = (index < lines.size()) ? lines[index] : 0;
			throw std::runtime_error(formatError(configPath, line, "unexpected '}'"));
		} 
		else if (tokens[index] == ";") {
			++index;
		} 
		else {
			std::size_t line = (index < lines.size()) ? lines[index] : 0;
			throw std::runtime_error(formatError(configPath, line, "unexpected token '" + tokens[index] + "'"));
		}
	}
}

static bool matchesLocation(const std::string &path, const std::string &location) {
	if (location.empty() || path.compare(0, location.size(), location) != 0)
		return false;
	if (location == "/" || path.size() == location.size())
		return true;
	return location[location.size() - 1] == '/' || path[location.size()] == '/';
}

static std::string extensionOf(const std::string &path) {
	std::string::size_type dot = path.find_last_of('.');
	std::string::size_type slash = path.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return path.substr(dot);
}

static bool hasUnsafeSegment(const std::string &path) {
	std::string::size_type start = 0;
	while (start < path.size()) {
		std::string::size_type end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		std::string segment = path.substr(start, end - start);
		if (segment == "." || segment == "..")
			return true;
		if (end == path.size())
			break;
		start = end + 1;
	}
	return false;
}

static bool resolveCgiScriptPath(const Route &route, const std::string &scriptName, std::string &scriptPath) {
	if (route.root.empty() || !matchesLocation(scriptName, route.location))
		return false;
	std::string relative = scriptName;
	while (!relative.empty() && relative[0] == '/')
		relative.erase(0, 1);
	if (relative.empty() || hasUnsafeSegment(relative))
		return false;
	scriptPath = route.root;
	if (scriptPath[scriptPath.size() - 1] != '/')
		scriptPath += '/';
	scriptPath += relative;
	return true;
}

static bool selectCgiHandler(const std::string &path,
	const std::map<std::string, std::string> &handlers, std::string &scriptName,
	std::string &handlerPath) {
	std::string::size_type end = path.find('/', 1);
	while (true) {
		std::string candidate = (end == std::string::npos) ? path : path.substr(0, end);
		std::map<std::string, std::string>::const_iterator handler =
			handlers.find(extensionOf(candidate));
		if (handler != handlers.end()) {
			scriptName = candidate;
			handlerPath = handler->second;
			return true;
		}
		if (end == std::string::npos)
			break;
		end = path.find('/', end + 1);
	}
	return false;
}

Route Config::route(const Request &request) const {
	if (server_configs.empty())
		return Route();

	return route(0, request);
}

Route Config::route(size_t serverIndex, const Request &request) const {
	if (serverIndex >= server_configs.size()) {
		Route fallback;
		fallback.location = "/";
		fallback.root = "./contents";
		fallback.allowed_methods.insert("GET");
		fallback.redirect_status = 0;
		fallback.autoindex = false;
		return fallback;
	}
	
	const ServerConfig &server = server_configs[serverIndex];
	const Route *best_route = NULL;
	std::string::size_type best_len = 0;
	
	for (std::vector<Route>::const_iterator rit = server.locations.begin(); rit != server.locations.end(); ++rit) {
		const std::string &candidate = rit->location;
		
		// Exact match has priority
		if (request.path == candidate && candidate.size() > best_len) {
			best_len = candidate.size();
			best_route = &(*rit);
			continue;
		}
		
		// Prefix match: longest match wins
		if (candidate != "/" && request.path.substr(0, candidate.size()) == candidate) {
			if (request.path.size() == candidate.size() || request.path[candidate.size()] == '/') {
				if (candidate.size() > best_len) {
					best_len = candidate.size();
					best_route = &(*rit);
				}
			}
		}
		
		// Root fallback
		if (candidate == "/" && best_route == NULL) {
			best_len = 1;
			best_route = &(*rit);
		}
	}
	
	if (best_route == NULL) {
		Route fallback;
		fallback.location = "/";
		fallback.root = server.root.empty() ? "./contents" : server.root;
		fallback.allowed_methods.insert("GET");
		fallback.allowed_methods.insert("POST");
		fallback.allowed_methods.insert("DELETE");
		fallback.redirect_status = 0;
		fallback.autoindex = false;
		return fallback;
	}
	
	Route result = *best_route;
	if (result.root.empty())
		result.root = server.root.empty() ? "./contents" : server.root;
	if (result.allowed_methods.empty())
		result.allowed_methods.insert("GET");

	// Reset request-specific CGI information
	result.is_cgi = false;
	result.cgi_handler.clear();
	result.cgi_script_name.clear();
	result.cgi_script_path.clear();

	// Resolve CGI handler and script path
	if (selectCgiHandler(
			request.path,
			result.cgi_handlers,
			result.cgi_script_name,
			result.cgi_handler)
		&& resolveCgiScriptPath(
			result,
			result.cgi_script_name,
			result.cgi_script_path)) {
		result.is_cgi = true;
	}
	return result;
}
