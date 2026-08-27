#include "Config.hpp"

static void allow(Route &route, const char *method) {
	route.allowed_methods.insert(method);
}

static Route makeRoute(const std::string &location, const std::string &root) {
	Route route;
	route.location = location;
	route.root = root;
	return route;
}

Config::Config(const std::string &configPath) {
	(void)configPath;
	// #4 will replace this explicit fixture with parser output for the same model.
	buildReferenceMock();
}

Config::~Config() {}

void Config::buildReferenceMock() {
	server_configs.clear();

	ServerConfig primary;
	primary.host = "0.0.0.0";
	primary.port = 8080;
	primary.server_name = "localhost";
	primary.root = "./contents";
	primary.client_max_body_size = 10000000;
	primary.error_pages[404] = "./contents/errors/404.html";
	primary.error_pages[500] = "./contents/errors/500.html";

	Route root = makeRoute("/", primary.root);
	allow(root, "GET");
	allow(root, "POST");
	allow(root, "DELETE");
	root.index_file = "index.html";
	root.cgi_handlers[".sh"] = "./contents/cgi/test.sh";
	primary.locations.push_back(root);

	Route gallery = makeRoute("/gallery", "./contents/gallery");
	allow(gallery, "GET");
	gallery.autoindex = true;
	gallery.index_file = "gallery.html";
	primary.locations.push_back(gallery);

	Route uploads = makeRoute("/uploads", primary.root);
	allow(uploads, "POST");
	uploads.upload_store = "./contents/uploads";
	primary.locations.push_back(uploads);

	Route redirect = makeRoute("/redirect", primary.root);
	allow(redirect, "GET");
	redirect.redirect_status = 302;
	redirect.redirect_target = "/gallery";
	primary.locations.push_back(redirect);

	server_configs.push_back(primary);

	ServerConfig secondary;
	secondary.host = "127.0.0.1";
	secondary.port = 8081;
	secondary.server_name = "secondary.local";
	secondary.root = "./contents/secondary";
	secondary.client_max_body_size = 1000000;
	secondary.error_pages[404] = "./contents/secondary/errors/404.html";

	Route secondaryRoot = makeRoute("/", secondary.root);
	allow(secondaryRoot, "GET");
	secondaryRoot.index_file = "index.html";
	secondary.locations.push_back(secondaryRoot);

	server_configs.push_back(secondary);
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

Route Config::route(const Request &request) const {
	if (server_configs.empty())
		return Route();

	const ServerConfig &server = server_configs[0];
	Route selected;
	bool found = false;
	for (std::vector<Route>::const_iterator it = server.locations.begin(); it != server.locations.end(); ++it) {
		if (!matchesLocation(request.path, it->location))
			continue;
		if (!found || it->location.size() > selected.location.size()) {
			selected = *it;
			found = true;
		}
	}
	if (!found)
		return Route();

	selected.is_cgi = false;
	selected.cgi_pass.clear();
	std::map<std::string, std::string>::const_iterator handler =
		selected.cgi_handlers.find(extensionOf(request.path));
	if (handler != selected.cgi_handlers.end()) {
		selected.is_cgi = true;
		selected.cgi_pass = handler->second;
	}
	return selected;
}

const std::vector<ServerConfig>& Config::servers() const {
	return server_configs;
}
