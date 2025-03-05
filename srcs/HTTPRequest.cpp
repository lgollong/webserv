#include "../headers/HTTPRequest.hpp"

HTTPRequest::HTTPRequest(){
	this->method = "";
	this->path = "";
	this->protocol = "";
	this->host = "";
	this->port = "";
	this->userAgent = "";
	this->acceptLanguage = "";
	this->acceptCharset = "";
	this->acceptEncoding = "";
	this->connection = "";
}

HTTPRequest::~HTTPRequest(){

}

void	HTTPRequest::parseRequest(std::string request){
	std::istringstream iss(request);
	std::string line;
	while (std::getline(iss, line)){
		if (line.empty())
			break;
		std::istringstream issLine(line);
		std::string key, value;
		if (issLine >> key >> value){
			if (key == "GET" || key == "POST" || key == "DELETE" || key == "PUT"){
				setMethod(key);
				setPath(value);
				setProtocol("HTTP/1.1");
			}
			else if (key == "Host:"){
				setHost(value.substr(0, value.find(':')));
				if (value.find(':') != std::string::npos){
					std::string port = value.substr(value.find(':') + 1);
					setPort(port);
				}
			}
			else if (key == "User-Agent:")
				setUserAgent(value);
			else if (key == "Accept-Language:")
				setAcceptLanguage(value);
			else if (key == "Accept-Charset:")
				setAcceptCharset(value);
			else if (key == "Accept-Encoding:")
				setAcceptEncoding(value);
			else if (key == "Connection:")
				setConnection(value);
		}
	}
}

void	HTTPRequest::printRequest(){
	std::cout << "----------------------------" << std::endl;
	std::cout << "Method: " << method << std::endl;
	std::cout << "Path: " << path << std::endl;
	std::cout << "Protocol: " << protocol << std::endl;
	std::cout << "Host: " << host << std::endl;
	std::cout << "Port: " << port << std::endl;
	std::cout << "User-Agent: " << userAgent << std::endl;
	std::cout << "Accept-Language: " << acceptLanguage << std::endl;
	std::cout << "Accept-Charset: " << acceptCharset << std::endl;
	std::cout << "Accept-Encoding: " << acceptEncoding << std::endl;
	std::cout << "Connection: " << connection << std::endl;
	std::cout << "----------------------------" << std::endl;
}

void	HTTPRequest::setMethod(std::string method){
	this->method = method;
}

void	HTTPRequest::setPath(std::string path){
	this->path = path;
}

void	HTTPRequest::setProtocol(std::string protocol){
	this->protocol = protocol;
}

void	HTTPRequest::setHost(std::string host){
	this->host = host;
}

void	HTTPRequest::setPort(std::string port){
	this->port = port;
}

void	HTTPRequest::setUserAgent(std::string userAgent){
	this->userAgent = userAgent;
}

void	HTTPRequest::setAcceptLanguage(std::string acceptLanguage){
	this->acceptLanguage = acceptLanguage;
}

void	HTTPRequest::setAcceptCharset(std::string acceptCharset){
	this->acceptCharset = acceptCharset;
}

void	HTTPRequest::setAcceptEncoding(std::string acceptEncoding){
	this->acceptEncoding = acceptEncoding;
}

void	HTTPRequest::setConnection(std::string connection){
	this->connection = connection;
}

std::string	HTTPRequest::getMethod() const{
	return method;
}

std::string	HTTPRequest::getPath() const{
	return path;
}

std::string	HTTPRequest::getProtocol() const{
	return protocol;
}

std::string	HTTPRequest::getHost() const{
	return host;
}

std::string	HTTPRequest::getPort() const{
	return port;
}

std::string	HTTPRequest::getUserAgent() const{
	return userAgent;
}

std::string	HTTPRequest::getAcceptLanguage() const{
	return acceptLanguage;
}

std::string	HTTPRequest::getAcceptCharset() const{
	return acceptCharset;
}

std::string	HTTPRequest::getAcceptEncoding() const{
	return acceptEncoding;
}

std::string	HTTPRequest::getConnection() const{
	return connection;
}

