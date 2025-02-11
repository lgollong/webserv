#ifndef MANAGE_SERVER_HPP
#define MANAGE_SERVER_HPP

#include "Webserv.hpp"

class Server;
class Client;

class ManageServer {
	public:
		ManageServer(std::vector<Server> servers);
		~ManageServer();

		void setupServers(std::vector<Server> &servers);
		void runServer();
		void disconnect(int i);
		void acceptConnection(int i);

		std::vector<Server> getServers() const;
	private:
		std::vector<pollfd> fds;
		std::vector<Server> servers;
		std::vector<Client> clients;
};

#endif