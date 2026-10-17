#include "../headers/StaticFile.hpp"

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
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

// serve() streams regular files through file_fd; tests read them back whole.
static Content serveFully(StaticFile &files, const Route &route, const Request &request) {
	Content content = files.serve(route, request);
	if (content.file_fd >= 0) {
		char buffer[4096];
		ssize_t count;
		while ((count = read(content.file_fd, buffer, sizeof(buffer))) > 0)
			content.body.append(buffer, static_cast<size_t>(count));
		close(content.file_fd);
		content.file_fd = -1;
	}
	return content;
}

static Request multipartRequest(const std::string &path, const std::string &contentType,
	const std::string &body) {
	Request request = requestFor(path);
	request.method = "POST";
	request.headers["content-type"] = contentType;
	request.body = body;
	return request;
}

static std::string filePart(const std::string &boundary, const std::string &filename,
	const std::string &data) {
	return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
		filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n" + data + "\r\n";
}

static bool fileContains(const std::string &path, const std::string &expected) {
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return false;
	std::string contents;
	char buffer[4096];
	ssize_t count;
	while ((count = read(fd, buffer, sizeof(buffer))) > 0)
		contents.append(buffer, static_cast<size_t>(count));
	close(fd);
	return count == 0 && contents == expected;
}

static bool missing(const std::string &path) {
	return access(path.c_str(), F_OK) != 0;
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

	Content text = serveFully(files, root, requestFor("/files/index.html"));
	expect(text.status == 200 && text.mime_type == "text/html" &&
		text.body.find("<h1>HI</h1>") != std::string::npos,
		"serves a text file from the route root");

	Content dashboardPage = serveFully(files, root, requestFor("/index.html"));
	expect(dashboardPage.status == 200 && dashboardPage.mime_type == "text/html" &&
		dashboardPage.body.find("browser-served project dashboard") != std::string::npos &&
		dashboardPage.body.find("Runtime contract and current boundaries") != std::string::npos,
		"serves the project dashboard from the default document root");
	Content dashboardStyle = serveFully(files, root, requestFor("/dashboard.css"));
	expect(dashboardStyle.status == 200 && dashboardStyle.mime_type == "text/css" &&
		!dashboardStyle.body.empty(), "serves the dashboard stylesheet");
	Content dashboardScript = serveFully(files, root, requestFor("/dashboard.js"));
	expect(dashboardScript.status == 200 && dashboardScript.mime_type == "application/javascript" &&
		!dashboardScript.body.empty(), "serves the dashboard request inspector script");

	Content binary = serveFully(files, root, requestFor("/files/favicon.ico"));
	expect(binary.status == 200 && binary.mime_type == "image/x-icon" &&
		binary.body.size() > 32 && binary.body[0] == '\0',
		"serves binary bytes with the matching MIME type");

	Content errorPage = files.readErrorPage("./contents/errors/404.html");
	expect(errorPage.status == 200 && errorPage.mime_type == "text/html" &&
		errorPage.body.find("Primary custom 404") != std::string::npos,
		"reads a configured regular error page with its MIME type");
	expect(files.readErrorPage("./contents/errors/missing.html").status != 200,
		"reports a missing configured error page without serving content");

	expect(serveFully(files, root, requestFor("/files/missing.txt")).status == 404,
		"missing files return 404");
	Content rootIndex = serveFully(files, root, requestFor("/"));
	expect(rootIndex.status == 200 && rootIndex.mime_type == "text/html" &&
		rootIndex.body.find("browser-served project dashboard") != std::string::npos,
		"serves the configured index for the route root");

	Content directoryIndex = serveFully(files, root, requestFor("/files"));
	expect(directoryIndex.status == 200 && directoryIndex.body.find("<title>Index Page</title>") != std::string::npos,
		"serves a configured index for a directory request");

	Route gallery = routeFor("/gallery", "./contents/gallery");
	gallery.index_file = "gallery.html";
	gallery.autoindex = true;
	Content listing = serveFully(files, gallery, requestFor("/gallery"));
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
	expect(serveFully(files, noIndex, requestFor("/no-index")).status == 403,
		"directories without an index require enabled autoindex");
	Route unsafeIndex = routeFor("/files", "./contents/files");
	unsafeIndex.index_file = "../AGENTS.md";
	expect(serveFully(files, unsafeIndex, requestFor("/files")).status == 403,
		"unsafe configured index paths are rejected");
	expect(serveFully(files, root, requestFor("/../AGENTS.md")).status == 403,
		"parent traversal is rejected before disk access");
	expect(serveFully(files, root, requestFor("/files/./index.html")).status == 403,
		"current-directory traversal is rejected before disk access");

	Route assets = routeFor("/assets", "./contents/files");
	Content stripped = serveFully(files, assets, requestFor("/assets/index.html"));
	expect(stripped.status == 200 && stripped.body.find("<title>Index Page</title>") != std::string::npos,
		"location prefix is stripped before resolving under the selected root");
	expect(serveFully(files, assets, requestFor("/files/index.html")).status == 403,
		"requests outside the selected location are rejected");
	expect(serveFully(files, routeFor("/", ""), requestFor("/files/index.html")).status == 403,
		"an empty root is rejected");

	const std::string fixturePath = "./contents/static-delete-fixture.txt";
	expect(writeFixture(fixturePath), "creates a disposable static delete fixture");
	Content deleted = files.erase(root, requestFor("/static-delete-fixture.txt"));
	expect(deleted.status == 204 && deleted.body.empty(), "deletes a regular route-relative file without a body");
	expect(serveFully(files, root, requestFor("/static-delete-fixture.txt")).status == 404,
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
	uploads.upload_enabled = true;
	Request upload = requestFor("/uploads/focused-upload.txt");
	upload.body = "focused upload body";
	expect(files.upload(uploads, upload).status == 201, "stores an authorized upload");
	expect(files.upload(uploads, upload).status == 409, "an existing upload target is a 409 conflict and is not overwritten");
	Content uploaded = serveFully(files, uploads, requestFor("/uploads/focused-upload.txt"));
	expect(uploaded.status == 200 && uploaded.body == "focused upload body",
		"upload route serves the stored file from upload_store at its upload URL");
	expect(serveFully(files, uploads, requestFor("/uploads/index.html")).status == 404,
		"upload route does not fall back to files under the route root");
	expect(files.erase(uploads, requestFor("/uploads/focused-upload.txt")).status == 204,
		"upload route deletes the stored file at its upload URL");
	expect(serveFully(files, uploads, requestFor("/uploads/focused-upload.txt")).status == 404,
		"deleted upload is no longer served");
	expect(files.upload(uploads, requestFor("/uploads/../unsafe.txt")).status == 400,
		"rejects traversal upload names");
	std::remove("./contents/uploads/focused-upload.txt");

	const std::string boundary = "----webservBoundary7MA4YWxk";
	const std::string multipartType = "multipart/form-data; boundary=" + boundary;
	const std::string binaryPayload("line one\r\n--not-a-boundary\r\n\0\x01\xff end", 37);
	std::string formBody = "preamble is ignored\r\n" +
		std::string("--") + boundary + "\r\nContent-Disposition: form-data; name=\"note\"\r\n\r\nplain field\r\n" +
		filePart(boundary, "multipart-binary.bin", binaryPayload) + "--" + boundary + "--\r\n";
	Content form = files.upload(uploads, multipartRequest("/uploads", multipartType, formBody));
	expect(form.status == 201 && form.location == "/uploads/multipart-binary.bin" &&
		form.body.find("href=\"/uploads/multipart-binary.bin\"") != std::string::npos,
		"multipart upload stores one file part and reports its URL");
	expect(fileContains("./contents/uploads/multipart-binary.bin", binaryPayload),
		"multipart upload stores only the byte-identical part payload");
	std::remove("./contents/uploads/multipart-binary.bin");

	std::string twoFiles = filePart(boundary, "multipart-one.txt", "one") +
		filePart(boundary, "C:\\Users\\me\\My Report (final).txt", "two") + "--" + boundary + "--\r\n";
	Content multiple = files.upload(uploads, multipartRequest("/uploads/", "Multipart/Form-Data; boundary=\"" +
		boundary + "\"", twoFiles));
	expect(multiple.status == 201 && multiple.location.empty(),
		"multipart upload stores several files without a single Location");
	expect(fileContains("./contents/uploads/multipart-one.txt", "one") &&
		fileContains("./contents/uploads/My_Report__final_.txt", "two"),
		"multipart filenames keep their base name with unsafe characters replaced");

	std::string conflicting = filePart(boundary, "multipart-new.txt", "new") +
		filePart(boundary, "multipart-one.txt", "again") + "--" + boundary + "--\r\n";
	expect(files.upload(uploads, multipartRequest("/uploads", multipartType, conflicting)).status == 409 &&
		missing("./contents/uploads/multipart-new.txt") && fileContains("./contents/uploads/multipart-one.txt", "one"),
		"failed multipart upload removes files stored earlier in the same request");
	std::remove("./contents/uploads/multipart-one.txt");
	std::remove("./contents/uploads/My_Report__final_.txt");

	expect(files.upload(uploads, multipartRequest("/uploads", "multipart/form-data", formBody)).status == 400,
		"multipart upload without a boundary is rejected");
	expect(files.upload(uploads, multipartRequest("/uploads", multipartType,
		filePart(boundary, "unterminated.txt", "data"))).status == 400 && missing("./contents/uploads/unterminated.txt"),
		"multipart upload without a closing delimiter is rejected");
	expect(files.upload(uploads, multipartRequest("/uploads", multipartType,
		"--" + boundary + "\r\nContent-Disposition: form-data; name=\"note\"\r\n\r\ntext\r\n--" + boundary + "--\r\n")).status == 400,
		"multipart upload without file parts is rejected");
	expect(files.upload(uploads, multipartRequest("/uploads", multipartType,
		filePart(boundary, "..", "x") + "--" + boundary + "--\r\n")).status == 400,
		"multipart upload rejects an unsafe filename");
	expect(files.upload(uploads, multipartRequest("/uploads", multipartType,
		filePart(boundary, "same.txt", "a") + filePart(boundary, "same.txt", "b") + "--" + boundary + "--\r\n")).status == 400 &&
		missing("./contents/uploads/same.txt"),
		"multipart upload rejects duplicate filenames before writing");
	expect(files.upload(uploads, multipartRequest("/uploads/named.txt", multipartType, formBody)).status == 400 &&
		missing("./contents/uploads/named.txt"),
		"multipart upload must target the upload location itself");

	expect(serveFully(files, root, requestFor("/index.html/below")).status == 404,
		"a path below a regular file is not found");
	Route missingStore = routeFor("/uploads", "./contents");
	missingStore.upload_store = "./contents/missing-upload-store";
	missingStore.upload_enabled = true;
	Request storeless = requestFor("/uploads/orphan.txt");
	storeless.body = "x";
	expect(files.upload(missingStore, storeless).status == 500,
		"a missing upload_store is a server-side 500, not a client 404");

	// Root ignores permission bits, so these cases only run unprivileged.
	if (geteuid() != 0) {
		const std::string lockedDir = "./contents/permission-fixture";
		mkdir(lockedDir.c_str(), 0700);
		expect(writeFixture(lockedDir + "/secret.txt"), "creates a permission fixture file");
		chmod((lockedDir + "/secret.txt").c_str(), 0000);
		expect(serveFully(files, root, requestFor("/permission-fixture/secret.txt")).status == 403,
			"an unreadable file is forbidden");
		chmod((lockedDir + "/secret.txt").c_str(), 0600);

		Route listing = routeFor("/", "./contents");
		listing.autoindex = true;
		chmod(lockedDir.c_str(), 0300);
		expect(serveFully(files, listing, requestFor("/permission-fixture")).status == 403,
			"an unreadable directory listing is forbidden");
		chmod(lockedDir.c_str(), 0500);
		expect(files.erase(root, requestFor("/permission-fixture/secret.txt")).status == 403,
			"deleting from a read-only directory is forbidden");
		Route lockedStore = routeFor("/uploads", "./contents");
		lockedStore.upload_store = lockedDir;
		lockedStore.upload_enabled = true;
		expect(files.upload(lockedStore, storeless).status == 403,
			"uploading into a read-only store is forbidden");
		chmod(lockedDir.c_str(), 0700);
		std::remove((lockedDir + "/secret.txt").c_str());
		rmdir(lockedDir.c_str());
	}

	Content streamed = files.serve(root, requestFor("/files/index.html"));
	expect(streamed.status == 200 && streamed.body.empty() && streamed.file_fd >= 0 &&
		streamed.file_size > 0, "regular files are returned as an open descriptor for streaming");
	if (streamed.file_fd >= 0)
		close(streamed.file_fd);

	if (failures == 0)
		std::cout << "static file tests passed" << std::endl;
	return failures == 0 ? 0 : 1;
}
