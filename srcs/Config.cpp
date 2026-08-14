#include "Config.hpp"

Config::Config(const std::string &configPath) {
	(void)configPath; // mock: no real config-file parsing yet

	ServerConfig server;
	server.host = "0.0.0.0";
	server.port = 8080;
	server.server_name = "localhost";

	Route root;
	root.root = "./sites";
	root.is_cgi = false;
	root.allowed_methods.insert("GET");
	root.allowed_methods.insert("POST");
	root.allowed_methods.insert("DELETE");
	server.locations.push_back(root);

	Route cgiLocation;
	cgiLocation.root = "./sites";
	cgiLocation.is_cgi = true;
	cgiLocation.cgi_pass = "/usr/bin/php-cgi";
	cgiLocation.allowed_methods.insert("POST");
	server.locations.push_back(cgiLocation);

	servers.push_back(server);
}

Config::~Config() {}

Route Config::route(const Request &request) const {
	// mock: no real config-file parsing / location matching yet.
	// pretend every request maps to one static location under ./sites,
	// except paths ending in .bla, which pretend to be CGI-backed --
	// gives Worker both branches to exercise (e.g. curl /index.html vs
	// curl /foo.bla).
	Route route;
	route.root = "./sites";
	route.allowed_methods.insert("GET");
	route.allowed_methods.insert("POST");
	route.allowed_methods.insert("DELETE");

	bool is_bla = request.path.size() >= 4 &&
		request.path.compare(request.path.size() - 4, 4, ".bla") == 0;

	if (is_bla) {
		route.is_cgi = true;
		route.cgi_pass = "/usr/bin/php-cgi";
	} else {
		route.is_cgi = false;
	}

	return route;
}
