#include "StaticFile.hpp"

StaticFile::StaticFile() {
	mime_types[".html"] = "text/html";
	mime_types[".htm"]  = "text/html";
	mime_types[".css"]  = "text/css";
	mime_types[".js"]   = "application/javascript";
	mime_types[".json"] = "application/json";
	mime_types[".png"]  = "image/png";
	mime_types[".jpg"]  = "image/jpeg";
	mime_types[".jpeg"] = "image/jpeg";
	mime_types[".gif"]  = "image/gif";
	mime_types[".ico"]  = "image/x-icon";
	mime_types[".txt"]  = "text/plain";
}

StaticFile::~StaticFile() {}

Content StaticFile::serve(const Route& route, const Request& request) {
	(void)route; // a real lookup will need route.root to resolve the path on disk

	Content content;
	content.status = 200;
	content.body =
		"<html><head><title>webserv</title></head><body>"
		"<h1>It works!</h1><p>mock content for " + request.path + "</p>"
		"</body></html>";

	std::string::size_type dot = request.path.find_last_of('.');
	std::string ext = (dot == std::string::npos) ? "" : request.path.substr(dot);
	std::map<std::string, std::string>::const_iterator it = mime_types.find(ext);
	content.mime_type = (it != mime_types.end()) ? it->second : "application/octet-stream";

	return content;
}
