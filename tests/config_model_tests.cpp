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
	Config config("./config/req.config");
	const std::vector<ServerConfig> &servers = config.servers();

	/* ============================================================
	 * SERVER CONFIGURATION
	 * ============================================================ */

	expect(servers.size() == 4,
		"req.config defines four server blocks");

	if (servers.size() >= 4) {
		/* Server 0 */
		expect(servers[0].host == "127.0.0.1" &&
			servers[0].port == 8002,
			"server 0 binds to 127.0.0.1:8002");

		expect(servers[0].server_name == "mywebsite.com",
			"server 0 name is parsed");

		expect(servers[0].client_max_body_size == 10485760,
			"server 0 body limit is 10M");

		/* Server 1 */
		expect(servers[1].host == "127.0.0.1" &&
			servers[1].port == 8003,
			"server 1 binds to 127.0.0.1:8003");

		expect(servers[1].server_name == "mywebsite.com",
			"server 1 name is parsed");

		expect(servers[1].client_max_body_size == 10485760,
			"server 1 body limit is 10M");

		/* Server 2 */
		expect(servers[2].host == "127.0.0.1" &&
			servers[2].port == 8008,
			"server 2 binds to 127.0.0.1:8008");

		expect(servers[2].server_name == "api.local",
			"server 2 name is parsed");

		expect(servers[2].client_max_body_size == 5242880,
			"server 2 body limit is 5M");

		/* Server 3 */
		expect(servers[3].host == "127.0.0.1" &&
			servers[3].port == 8001,
			"server 3 binds to 127.0.0.1:8001");

		expect(servers[3].server_name == "admin.local",
			"server 3 name is parsed");

		expect(servers[3].client_max_body_size == 2097152,
			"server 3 body limit is 2M");

		/* Location counts */
		expect(servers[0].locations.size() == 6,
			"server 0 has six locations");

		expect(servers[1].locations.size() == 6,
			"server 1 has six locations");

		expect(servers[2].locations.size() == 3,
			"server 2 has three locations");

		expect(servers[3].locations.size() == 2,
			"server 3 has two locations");
	}

	/* ============================================================
	 * SERVER-LEVEL API
	 * ============================================================ */

	expect(config.bodyLimit(0) == 10485760,
		"bodyLimit(0) returns 10M");

	expect(config.bodyLimit(1) == 10485760,
		"bodyLimit(1) returns 10M");

	expect(config.bodyLimit(2) == 5242880,
		"bodyLimit(2) returns 5M");

	expect(config.bodyLimit(3) == 2097152,
		"bodyLimit(3) returns 2M");

	expect(config.bodyLimit(4) == 1000000,
		"out-of-range bodyLimit falls back to 1MB");

	/* ============================================================
	 * ERROR PAGES
	 * ============================================================ */

	expect(config.errorPage(0, 404) ==
		"./contents/errors/404.html",
		"server 0 has correct 404 error page");

	expect(config.errorPage(0, 500) ==
		"./contents/errors/500.html",
		"server 0 has correct 500 error page");

	expect(config.errorPage(1, 404) ==
		"./contents/errors/404.html",
		"server 1 has correct 404 error page");

	expect(config.errorPage(1, 500) ==
		"./contents/errors/500.html",
		"server 1 has correct 500 error page");

	expect(config.errorPage(2, 404) ==
		"./contents/errors/404.html",
		"server 2 has correct 404 error page");

	expect(config.errorPage(2, 500) ==
		"./contents/errors/500.html",
		"server 2 has correct 500 error page");

	expect(config.errorPage(3, 404) ==
		"./contents/errors/404.html",
		"server 3 has correct 404 error page");

	expect(config.errorPage(3, 500) ==
		"./contents/errors/500.html",
		"server 3 has correct 500 error page");

	expect(config.errorPage(4, 404).empty(),
		"invalid server index has no error page");

	/* ============================================================
	 * SERVER 0 / PORT 8002
	 * ============================================================ */

	Route root = config.route(0, requestFor("/"));

	expect(root.location == "/" &&
		root.root == "./contents",
		"server 0 root route has correct location and root");

	expect(allows(root, "GET") &&
		allows(root, "POST") &&
		allows(root, "DELETE"),
		"server 0 root allows GET POST DELETE");

	expect(root.index_file == "index.html" &&
		!root.autoindex,
		"server 0 root has index.html and autoindex off");

	Route files = config.route(0, requestFor("/static/index.html"));

	expect(files.location == "/static" &&
		files.root == "./contents/files" &&
		files.autoindex,
		"server 0 static route has correct root and autoindex");

	expect(allows(files, "GET") &&
		!allows(files, "POST") &&
		!allows(files, "DELETE"),
		"server 0 static route is GET-only");

	Route upload = config.route(0, requestFor("/uploads/report.txt"));

	expect(upload.location == "/uploads" &&
		upload.root == "./contents" &&
		upload.upload_store == "./contents/uploads",
		"server 0 upload route has correct root and upload store");

	expect(allows(upload, "POST") &&
		allows(upload, "DELETE") &&
		!allows(upload, "GET"),
		"server 0 upload route allows POST DELETE only");

	Route cgi = config.route(0, requestFor("/cgi/test.php"));

	expect(cgi.location == "/cgi",
		"server 0 PHP request selects cgi route");

	expect(cgi.is_cgi,
		"server 0 PHP request is marked as CGI");

	expect(cgi.cgi_handlers.size() == 3,
		"server 0 cgi contains three CGI handlers");

	expect(cgi.cgi_handlers.find(".php") != cgi.cgi_handlers.end() &&
		cgi.cgi_handlers.find(".py") != cgi.cgi_handlers.end() &&
		cgi.cgi_handlers.find(".sh") != cgi.cgi_handlers.end(),
		"server 0 contains PHP Python and SH CGI mappings");

	expect(cgi.cgi_handlers.find(".php")->second ==
		"/usr/bin/php-cgi",
		"PHP CGI handler is /usr/bin/php-cgi");

	expect(cgi.cgi_handlers.find(".py")->second ==
		"/usr/bin/python3",
		"Python CGI handler is /usr/bin/python3");

	expect(cgi.cgi_handlers.find(".sh")->second ==
		"/bin/bash",
		"SH CGI handler is /bin/bash");

	expect(allows(cgi, "GET") &&
		allows(cgi, "POST") &&
		!allows(cgi, "DELETE"),
		"server 0 cgi allows GET POST only");

	expect(cgi.cgi_script_name == "/cgi/test.php",
		"PHP CGI script name is resolved");

	expect(cgi.cgi_handler == "/usr/bin/php-cgi",
		"PHP CGI handler is resolved");

	/*
	 * The parser currently gives locations without an explicit
	 * root the default "./contents".
	 */
	expect(cgi.root == "./contents",
		"server 0 CGI route uses default location root");

	Route redirect = config.route(0, requestFor("/redirect-test"));

	expect(redirect.location == "/redirect-test" &&
		redirect.redirect_status == 301 &&
		redirect.redirect_target == "/",
		"server 0 redirect route parses 301 and target");

	expect(allows(redirect, "GET"),
		"server 0 redirect route allows GET by default");

	Route docs = config.route(0, requestFor("/docs/readme.html"));

	expect(docs.location == "/docs" &&
		docs.root == "./contents" &&
		docs.autoindex &&
		docs.index_file == "index.html",
		"server 0 docs route parses root index and autoindex");

	expect(allows(docs, "GET") &&
		!allows(docs, "POST"),
		"server 0 docs route is GET-only");

	/* ============================================================
	 * SERVER 1 / PORT 8003
	 * ============================================================ */

	Route root8003 = config.route(1, requestFor("/"));

	expect(root8003.location == "/" &&
		root8003.root == "./contents",
		"server 1 root route is correct");

	expect(allows(root8003, "GET") &&
		allows(root8003, "POST") &&
		allows(root8003, "DELETE"),
		"server 1 root allows GET POST DELETE");

	Route static8003 = config.route(1, requestFor("/static/file.txt"));

	expect(static8003.location == "/static" &&
		static8003.root == "./contents/files" &&
		static8003.autoindex,
		"server 1 static route is correct");

	Route upload8003 = config.route(1, requestFor("/uploads/file.txt"));

	expect(upload8003.location == "/uploads" &&
		upload8003.root == "./contents" &&
		upload8003.upload_store == "./contents/uploads",
		"server 1 upload route is correct");

	expect(allows(upload8003, "POST") &&
		allows(upload8003, "DELETE") &&
		!allows(upload8003, "GET"),
		"server 1 upload route allows POST DELETE");

	Route cgi8003 = config.route(1, requestFor("/cgi/test.py"));

	expect(cgi8003.location == "/cgi" &&
		cgi8003.is_cgi &&
		cgi8003.cgi_handlers.size() == 3,
		"server 1 CGI route is parsed");

	expect(cgi8003.cgi_handlers.find(".py") !=
		cgi8003.cgi_handlers.end(),
		"server 1 Python CGI mapping exists");

	expect(cgi8003.cgi_handlers.find(".py")->second ==
		"/usr/bin/python3",
		"server 1 Python CGI handler is correct");

	expect(cgi8003.cgi_script_name == "/cgi/test.py",
		"server 1 Python CGI script name is resolved");

	expect(cgi8003.cgi_handler == "/usr/bin/python3",
		"server 1 Python CGI handler is resolved");

	Route redirect8003 = config.route(1, requestFor("/redirect-test"));

	expect(redirect8003.location == "/redirect-test" &&
		redirect8003.redirect_status == 301 &&
		redirect8003.redirect_target == "/",
		"server 1 redirect route is correct");

	Route docs8003 = config.route(1, requestFor("/docs/test.html"));

	expect(docs8003.location == "/docs" &&
		docs8003.root == "./contents" &&
		docs8003.autoindex,
		"server 1 docs route is correct");

	/* ============================================================
	 * SERVER 2 / PORT 8008 / api.local
	 * ============================================================ */

	Route apiRoot = config.route(2, requestFor("/"));

	expect(apiRoot.location == "/" &&
		apiRoot.root == "./contents/secondary",
		"server 2 root uses secondary document root");

	expect(allows(apiRoot, "GET") &&
		allows(apiRoot, "POST") &&
		!allows(apiRoot, "DELETE"),
		"server 2 root allows GET POST only");

	expect(apiRoot.index_file == "index.html",
		"server 2 root has index.html");

	Route api = config.route(2, requestFor("/api/resource"));

	/*
	 * /api has no root directive.
	 * parseLocation() therefore assigns "./contents"; autoindex is enabled.
	 */
	expect(api.location == "/api" &&
		api.root == "./contents" &&
		api.autoindex,
		"server 2 /api route resolves correctly");

	expect(allows(api, "GET") &&
		allows(api, "POST") &&
		allows(api, "PUT") &&
		allows(api, "DELETE"),
		"server 2 /api allows GET POST PUT DELETE");

	Route apiUpload = config.route(2, requestFor("/api/upload/file.txt"));

	/*
	 * /api/upload also has no root directive,
	 * therefore its parsed root is "./contents".
	 */
	expect(apiUpload.location == "/api/upload" &&
		apiUpload.root == "./contents" &&
		apiUpload.upload_store == "./contents/uploads",
		"server 2 /api/upload route resolves correctly");

	expect(allows(apiUpload, "POST") &&
		!allows(apiUpload, "GET") &&
		!allows(apiUpload, "PUT") &&
		!allows(apiUpload, "DELETE"),
		"server 2 /api/upload keeps POST-only policy");

	/*
	 * Longest-prefix matching:
	 * /api/upload/file.txt must select /api/upload,
	 * not /api.
	 */
	expect(apiUpload.location == "/api/upload",
		"server 2 uses longest matching location");

	expect(api.location == "/api",
		"server 2 /api request selects /api");

	/* ============================================================
	 * SERVER 3 / PORT 8001 / admin.local
	 * ============================================================ */

	Route adminRoot = config.route(3, requestFor("/"));

	expect(adminRoot.location == "/" &&
		adminRoot.root == "./contents",
		"server 3 root route is correct");

	expect(allows(adminRoot, "GET") &&
		allows(adminRoot, "POST") &&
		allows(adminRoot, "DELETE"),
		"server 3 root allows GET POST DELETE");

	Route adminCgi = config.route(3, requestFor("/cgi/test.py"));

	expect(adminCgi.location == "/cgi" &&
		adminCgi.is_cgi &&
		adminCgi.cgi_handlers.size() == 2,
		"server 3 CGI route contains two handlers");

	expect(adminCgi.cgi_handlers.find(".php") !=
		adminCgi.cgi_handlers.end(),
		"server 3 contains PHP CGI mapping");

	expect(adminCgi.cgi_handlers.find(".php")->second ==
		"/usr/bin/php-cgi",
		"server 3 PHP CGI handler is correct");

	expect(adminCgi.cgi_handlers.find(".py") !=
		adminCgi.cgi_handlers.end(),
		"server 3 contains Python CGI mapping");

	expect(adminCgi.cgi_handlers.find(".py")->second ==
		"/usr/bin/python3",
		"server 3 Python CGI handler is correct");

	expect(adminCgi.cgi_script_name == "/cgi/test.py",
		"server 3 Python CGI script name is resolved");

	expect(adminCgi.cgi_handler == "/usr/bin/python3",
		"server 3 Python CGI handler is resolved");

	expect(allows(adminCgi, "GET") &&
		allows(adminCgi, "POST") &&
		!allows(adminCgi, "DELETE"),
		"server 3 CGI route allows GET POST only");

	/* ============================================================
	 * LOCATION BOUNDARY TESTS
	 * ============================================================ */

	Route boundary = config.route(0, requestFor("/static-file.txt"));

	expect(boundary.location == "/",
		"/static-file.txt must not match /static");

	Route uploadBoundary = config.route(0, requestFor("/uploads-other/file.txt"));

	expect(uploadBoundary.location == "/",
		"/uploads-other must not match /uploads");

	Route apiBoundary = config.route(2, requestFor("/api-other/resource"));

	expect(apiBoundary.location == "/",
		"/api-other must not match /api");

	/* ============================================================
	 * CGI SELECTION TESTS
	 * ============================================================ */

	Route phpCgi = config.route(0, requestFor("/cgi/test.php"));

	expect(phpCgi.is_cgi,
		"PHP request is selected as CGI");

	expect(phpCgi.cgi_script_name == "/cgi/test.php",
		"PHP script name is correct");

	expect(phpCgi.cgi_handler == "/usr/bin/php-cgi",
		"PHP extension selects php-cgi");

	Route pythonCgi = config.route(0, requestFor("/cgi/test.py"));

	expect(pythonCgi.is_cgi,
		"Python request is selected as CGI");

	expect(pythonCgi.cgi_script_name == "/cgi/test.py",
		"Python script name is correct");

	expect(pythonCgi.cgi_handler == "/usr/bin/python3",
		"PY extension selects python3");

	Route shellCgi = config.route(0, requestFor("/cgi/test.sh"));

	expect(shellCgi.is_cgi,
		"SH request is selected as CGI");

	expect(shellCgi.cgi_script_name == "/cgi/test.sh",
		"SH script name is correct");

	expect(shellCgi.cgi_handler == "/bin/bash",
		"SH extension selects /bin/bash");

	/*
	 * Unknown extension must not select CGI.
	 */
	Route unknownCgi = config.route(0, requestFor("/cgi/test.txt"));

	expect(!unknownCgi.is_cgi,
		"unknown CGI extension is not treated as CGI");

	expect(unknownCgi.cgi_script_name.empty() &&
		unknownCgi.cgi_handler.empty() &&
		unknownCgi.cgi_script_path.empty(),
		"unknown CGI extension has empty CGI request fields");

	/*
	 * PATH_INFO:
	 * /cgi/test.py/foo
	 * should resolve:
	 *
	 * SCRIPT_NAME = /cgi/test.py
	 * PATH_INFO   = /foo
	 */
	Route cgiPathInfo = config.route(
		0,
		requestFor("/cgi/test.py/foo")
	);

	expect(cgiPathInfo.is_cgi,
		"CGI request with PATH_INFO is selected");

	expect(cgiPathInfo.cgi_script_name == "/cgi/test.py",
		"CGI script name excludes PATH_INFO");

	expect(cgiPathInfo.cgi_handler == "/usr/bin/python3",
		"CGI with PATH_INFO keeps Python handler");

	/*
	 * CGI selection must reject traversal.
	 */
	Route traversalCgi = config.route(
		0,
		requestFor("/cgi/../test.py")
	);

	expect(!traversalCgi.is_cgi,
		"CGI traversal request is not selected as CGI");

	/* ============================================================
	 * INVALID SERVER INDEX
	 * ============================================================ */

	Route invalidServer = config.route(99, requestFor("/anything"));

	expect(invalidServer.location == "/" &&
		invalidServer.root == "./contents" &&
		allows(invalidServer, "GET"),
		"invalid server index falls back to default route");

	/* ============================================================
	 * DEFAULT route(request)
	 * ============================================================ */

	Route defaultRoute = config.route(
		requestFor("/static/index.html")
	);

	expect(defaultRoute.location == "/static",
		"route(request) uses server zero");

	expect(defaultRoute.root == "./contents/files",
		"default route(request) uses server zero configuration");

	/* ============================================================
	 * RESULT
	 * ============================================================ */

	if (failures == 0)
		std::cout << "configuration model tests passed" << std::endl;

	return failures == 0 ? 0 : 1;
}
