#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include "Webserv.hpp"

class HTTPRequest;

class HTTPResponse {
	public:
		HTTPResponse();
		~HTTPResponse();

		void	sendResponse(int fd);
		void	parseResponse(HTTPRequest request);

		void	setStatus(std::string status);
		void	setContentType(std::string contentType);
		void	setContentLength(std::string contentLength);
		void	setBody(std::string body);
		void	setBodyFromFile(std::string path);

		std::string	getStatus() const;
		std::string	getContentType() const;
		std::string	getContentLength() const;
		std::string	getBody() const;

	private:
		std::string	status;
		std::string	contentType;
		std::string	contentLength;
		std::string	body;
};

#endif