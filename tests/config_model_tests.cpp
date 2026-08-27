#include "../headers/Config.hpp"

#include <iostream>

static int failures = 0;

static void expect(bool condition, const char *message) {
	if (condition)
		return;
	std::cerr << "failure: " << message << std::endl;
	++failures;
}

static Request requestFor(const std::string &path) {
	Request request;
	request.path = path;
	return request;
}

static bool allows(const Route &route, const char *method) {
	return route.allowed_methods.find(method) != route.allowed_methods.end();
}

int main() {
	Config config("ignored-by-reference-mock");
	const std::vector<ServerConfig> &servers = config.servers();
	expect(servers.size() == 2, "reference mock defines multiple listeners");
	if (servers.size() >= 2) {
		expect(servers[0].host == "0.0.0.0" && servers[0].port == 8080,
			"primary listener is configured");
		expect(servers[0].client_max_body_size == 10000000,
			"primary request body limit is configured");
		expect(servers[0].error_pages.find(404) != servers[0].error_pages.end(),
			"primary error page mapping is configured");
		expect(servers[1].host == "127.0.0.1" && servers[1].port == 8081,
			"secondary listener is configured");
	}

	Route gallery = config.route(requestFor("/gallery/photos/image.jpg"));
	expect(gallery.location == "/gallery" && gallery.root == "./contents/gallery",
		"longest matching location selects gallery route");
	expect(gallery.autoindex && gallery.index_file == "gallery.html" && allows(gallery, "GET"),
		"gallery route supplies directory settings and methods");

	Route upload = config.route(requestFor("/uploads/report.txt"));
	expect(upload.location == "/uploads" && upload.upload_store == "./contents/uploads",
		"upload route supplies storage location");
	expect(allows(upload, "POST") && !allows(upload, "GET"),
		"upload route supplies method policy");

	Route redirect = config.route(requestFor("/redirect"));
	expect(redirect.redirect_status == 302 && redirect.redirect_target == "/gallery" && allows(redirect, "GET"),
		"redirect route supplies redirect status, target, and method policy");

	Route cgi = config.route(requestFor("/test.sh"));
	expect(cgi.location == "/" && cgi.is_cgi && cgi.cgi_pass == "./contents/cgi/test.sh",
		"CGI extension selects the configured handler");

	Route boundary = config.route(requestFor("/uploads-elsewhere"));
	expect(boundary.location == "/", "location prefix matching respects path boundaries");

	Route fallback = config.route(requestFor("/unknown"));
	expect(fallback.location == "/" && !fallback.is_cgi && allows(fallback, "DELETE"),
		"root route remains the static method-policy fallback");

	if (failures == 0)
		std::cout << "configuration model tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
