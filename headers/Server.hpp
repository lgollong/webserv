#ifndef SERVER_HPP
#define SERVER_HPP

#include "Webserv.hpp"

class Location;

class Server {
	public: 
		Server();
		~Server();

		void	printServerConfig();

		void	setServSocket(int servSocket);
		void	setServAddress(sockaddr_in servAddress);
		void	setHost(in_addr_t host);
		void	setPort(uint16_t port);
		void	setServerName(std::string server_name);
		void	setRoot(std::string root);
		void	setClientMaxBodySize(unsigned long client_max_body_size);
		void	setServId(int servId);
		void	setIndex(std::string index);
		void	setLocations(Location locations);
		void	setErrorPages(int error, std::string page);

		int						getServSocket() const;
		sockaddr_in				getServAddress() const;
		int						getServId() const;
		in_addr_t				getHost() const;
		uint16_t				getPort() const;
		std::string				getServerName() const;
		std::string				getRoot() const;
		std::vector<Location>	getLocations() const;
		unsigned long			getClientMaxBodySize() const;
		std::string				getIndex() const;
		std::string				getErrorPage(int error) const;
	private:
		int							servSocket;
		sockaddr_in					servAddress;
		int							servId;
		in_addr_t					host;
		uint16_t					port;
		unsigned long				clientMaxBodySize;
		std::string					serverName;
		std::string					root;
		std::vector<Location>		locations;
		std::string					index;
		std::map<int, std::string>	errorPages;
};

#endif