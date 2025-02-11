#ifndef SERVER_HPP
#define SERVER_HPP

#include "Webserv.hpp"

typedef struct Location {
	std::string											path;
	std::map<std::string, std::vector<std::string> >	options;
}				Location;

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
		void	setLocations(std::string locationPath, std::map<std::string, std::vector<std::string> > locationConfig);
		void	setClientMaxBodySize(unsigned long client_max_body_size);
		void	setServId(int servId);
		void	setIndex(std::string index);

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
	private:
		int						servSocket;
		sockaddr_in				servAddress;
		int						servId;
		in_addr_t				host;
		uint16_t				port;
		unsigned long			client_max_body_size;
		std::string				server_name;
		std::string				root;
		std::vector<Location>	locations;
		std::string				index;
};

#endif