#include "../headers/StaticFile.hpp"

#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

static int failures = 0;

static void expect(bool condition, const char *message) {
	if (condition)
		return;
	std::cerr << "failure: " << message << std::endl;
	++failures;
}

static Route routeFor(const std::string &location, const std::string &root) {
	Route route;
	route.location = location;
	route.root = root;
	return route;
}

static Request requestFor(const std::string &path) {
	Request request;
	request.path = path;
	return request;
}

static bool writeFixture(const std::string &path) {
	int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return false;
	const char body[] = "disposable static delete fixture\n";
	ssize_t written = write(fd, body, sizeof(body) - 1);
	close(fd);
	return written == static_cast<ssize_t>(sizeof(body) - 1);
}

int main() {
	StaticFile files;
	Route root = routeFor("/", "./contents");
	root.index_file = "index.html";

	Content text = files.serve(root, requestFor("/files/index.html"));
	expect(text.status == 200 && text.mime_type == "text/html" &&
		text.body.find("<h1>HI</h1>") != std::string::npos,
		"serves a text file from the route root");

	Content binary = files.serve(root, requestFor("/files/favicon.ico"));
	expect(binary.status == 200 && binary.mime_type == "image/x-icon" &&
		binary.body.size() > 32 && binary.body[0] == '\0',
		"serves binary bytes with the matching MIME type");

	Content errorPage = files.readErrorPage("./contents/errors/404.html");
	expect(errorPage.status == 200 && errorPage.mime_type == "text/html" &&
		errorPage.body.find("Primary custom 404") != std::string::npos,
		"reads a configured regular error page with its MIME type");
	expect(files.readErrorPage("./contents/errors/missing.html").status != 200,
		"reports a missing configured error page without serving content");

	expect(files.serve(root, requestFor("/files/missing.txt")).status == 404,
		"missing files return 404");
	Content rootIndex = files.serve(root, requestFor("/"));
	expect(rootIndex.status == 200 && rootIndex.mime_type == "text/html" &&
		rootIndex.body.find("Root index") != std::string::npos,
		"serves the configured index for the route root");

	Content directoryIndex = files.serve(root, requestFor("/files"));
	expect(directoryIndex.status == 200 && directoryIndex.body.find("<title>Index Page</title>") != std::string::npos,
		"serves a configured index for a directory request");

	Route gallery = routeFor("/gallery", "./contents/gallery");
	gallery.index_file = "gallery.html";
	gallery.autoindex = true;
	Content listing = files.serve(gallery, requestFor("/gallery"));
	std::string::size_type alpha = listing.body.find("alpha.txt");
	std::string::size_type nested = listing.body.find("nested/");
	expect(listing.status == 200 && listing.mime_type == "text/html" &&
		listing.body.find("href=\"/gallery/alpha.txt\"") != std::string::npos &&
		listing.body.find("href=\"/gallery/ampersand%26entry.txt\"") != std::string::npos &&
		listing.body.find("ampersand&amp;entry.txt") != std::string::npos &&
		alpha != std::string::npos && nested != std::string::npos && alpha < nested,
		"autoindex lists deterministic, escaped route entries");

	Route noIndex = routeFor("/no-index", "./contents/no-index");
	noIndex.index_file = "index.html";
	expect(files.serve(noIndex, requestFor("/no-index")).status == 403,
		"directories without an index require enabled autoindex");
	Route unsafeIndex = routeFor("/files", "./contents/files");
	unsafeIndex.index_file = "../AGENTS.md";
	expect(files.serve(unsafeIndex, requestFor("/files")).status == 403,
		"unsafe configured index paths are rejected");
	expect(files.serve(root, requestFor("/../AGENTS.md")).status == 403,
		"parent traversal is rejected before disk access");
	expect(files.serve(root, requestFor("/files/./index.html")).status == 403,
		"current-directory traversal is rejected before disk access");

	Route assets = routeFor("/assets", "./contents/files");
	Content stripped = files.serve(assets, requestFor("/assets/index.html"));
	expect(stripped.status == 200 && stripped.body.find("<title>Index Page</title>") != std::string::npos,
		"location prefix is stripped before resolving under the selected root");
	expect(files.serve(assets, requestFor("/files/index.html")).status == 403,
		"requests outside the selected location are rejected");
	expect(files.serve(routeFor("/", ""), requestFor("/files/index.html")).status == 403,
		"an empty root is rejected");

	const std::string fixturePath = "./contents/static-delete-fixture.txt";
	expect(writeFixture(fixturePath), "creates a disposable static delete fixture");
	Content deleted = files.erase(root, requestFor("/static-delete-fixture.txt"));
	expect(deleted.status == 204 && deleted.body.empty(), "deletes a regular route-relative file without a body");
	expect(files.serve(root, requestFor("/static-delete-fixture.txt")).status == 404,
		"deleted static file is no longer served");
	expect(files.erase(root, requestFor("/files")).status == 403,
		"directory deletion is rejected");
	expect(files.erase(root, requestFor("/missing-delete.txt")).status == 404,
		"missing delete target returns 404");
	expect(files.erase(root, requestFor("/../AGENTS.md")).status == 403,
		"delete traversal is rejected before disk access");
	std::remove(fixturePath.c_str());

	Route uploads = routeFor("/uploads", "./contents");
	uploads.upload_store = "./contents/uploads";
	Request upload = requestFor("/uploads/focused-upload.txt");
	upload.body = "focused upload body";
	expect(files.upload(uploads, upload).status == 201, "stores an authorized upload");
	expect(files.upload(uploads, upload).status == 403, "does not overwrite an existing upload");
	expect(files.upload(uploads, requestFor("/uploads/../unsafe.txt")).status == 400,
		"rejects traversal upload names");
	std::remove("./contents/uploads/focused-upload.txt");

	if (failures == 0)
		std::cout << "static file tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
