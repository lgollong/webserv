#include "../headers/HTTPResponse.hpp"

HTTPResponse::HTTPResponse(){
	this->status = "";
	this->contentType = "";
	this->contentLength = "";
	this->body = "";
}

HTTPResponse::~HTTPResponse(){
	
}

void	HTTPResponse::sendResponse(int fd){
	std::string response = "HTTP/1.1 " + status + "\r\n";
	response += "Content-Type: " + contentType + "\r\n";
	response += "Content-Length: " + contentLength + "\r\n";
	response += "\r\n";
	response += body;
	send(fd, response.c_str(), response.size(), 0);
}

void	HTTPResponse::parseResponse(HTTPRequest request){
	std::string path = request.getPath();
	if (path == "/"){
		setBodyFromFile("/Users/elgollong/VScode/webserv/sites/index.html");
	} else if (path == "/favicon.ico"){
		setBodyFromFile("/Users/elgollong/VScode/webserv/sites/favicon.ico");
	} else {
		setBodyFromFile(path.substr(1));
	}
	std::string method = request.getMethod();
	setContentType("text/html");
	setContentLength(std::to_string(body.size()));
	if (method == "GET" || method == "POST" || method == "DELETE" || method == "PUT"){
		setStatus("200 OK");
	} else {
		setStatus("405 Method Not Allowed");
	}
}

void	HTTPResponse::setStatus(std::string status){
	this->status = status;
}

void	HTTPResponse::setContentType(std::string contentType){
	this->contentType = contentType;
}

void	HTTPResponse::setContentLength(std::string contentLength){
	this->contentLength = contentLength;
}

void	HTTPResponse::setBody(std::string body){
	this->body = body;
}

void	HTTPResponse::setBodyFromFile(std::string path){
	std::ifstream fileStream(path.c_str());
	if (!fileStream.is_open())
		throw std::runtime_error("Error: can not open file");
	std::string line;
	while (std::getline(fileStream, line)){
		body += line;
		body += "\n";
	}
}

std::string	HTTPResponse::getStatus() const{
	return status;
}

std::string	HTTPResponse::getContentType() const{
	return contentType;
}

std::string	HTTPResponse::getContentLength() const{
	return contentLength;
}

std::string	HTTPResponse::getBody() const{
	return body;
}

