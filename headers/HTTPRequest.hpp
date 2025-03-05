#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include "Webserv.hpp"

class HTTPRequest {
	public:
		HTTPRequest();
		~HTTPRequest();

		void	parseRequest(std::string request);
		void	printRequest();
		void	setMethod(std::string method);
		void	setPath(std::string path);
		void	setProtocol(std::string protocol);
		void	setHost(std::string host);
		void	setPort(std::string port);
		void	setUserAgent(std::string userAgent);
		void	setAcceptLanguage(std::string acceptLanguage);
		void	setAcceptCharset(std::string acceptCharset);
		void	setAcceptEncoding(std::string acceptEncoding);
		void	setConnection(std::string connection);

		std::string	getMethod() const;
		std::string	getPath() const;
		std::string	getProtocol() const;
		std::string	getHost() const;
		std::string	getPort() const;
		std::string	getUserAgent() const;
		std::string	getAcceptLanguage() const;
		std::string	getAcceptCharset() const;
		std::string	getAcceptEncoding() const;
		std::string	getConnection() const;

	private:
		std::string	method;
		std::string	path;
		std::string	protocol;
		std::string	host;
		std::string	port;
		std::string	userAgent;
		std::string	acceptLanguage;
		std::string	acceptCharset;
		std::string	acceptEncoding;
		std::string	connection;
};

#endif