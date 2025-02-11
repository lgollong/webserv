#include "../headers/ManageServer.hpp"

ManageServer::ManageServer(std::vector<Server> servers){
	this->servers = servers;
}

ManageServer::~ManageServer(){

}

void	ManageServer::disconnect(int i){
	if (fds[i].fd > 2)
		close(fds[i].fd);
	fds.erase(fds.begin() + i);
	fds.shrink_to_fit();
	clients.erase(clients.begin() + i - servers.size());
	clients.shrink_to_fit();
}

void	ManageServer::acceptConnection(int i){
	sockaddr_in clientAddress;
	socklen_t clientAddressSize = sizeof(clientAddress);
	int clientSocket = accept(fds[i].fd, (sockaddr *)&clientAddress, &clientAddressSize);
	fcntl(clientSocket, F_SETFL, O_NONBLOCK);
	if (clientSocket ==  -1)
		throw std::runtime_error("Error: client connection failed");
	else
		std::cout << "Client connected!" << " using the fd: " << clientSocket <<std::endl;
	pollfd new_fd = {clientSocket, POLLIN, 0};
	fds.push_back(new_fd);
	Client client(clientSocket, clientAddress, fds.size() - servers.size());
	clients.push_back(client);
	client.printClientConfig();
}

void ManageServer::runServer(){
	std::cout << "--- Server started ---" << std::endl;
	std::cout << "Waiting for connections..." << std::endl;
	for (size_t i = 0; i < servers.size(); i++) {
		std::cout << "Server " << servers[i].getServId() << " listening on port " << servers[i].getPort() << std::endl;
	}
	while (1) {
		int pollVal = poll(fds.data(), fds.size(), 5000);
		std::cout << "Current connections: " << fds.size() - servers.size() << std::endl;
		if (pollVal == -1)
			break ;
		for (size_t i = 0; i < fds.size(); i++) {
			if (i && fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
				disconnect(i);
			else if (fds[i].revents & POLLIN) {
				if (i < servers.size())
					acceptConnection(i);
				else {
				}
			}
		}
	}
}

void ManageServer::setupServers(std::vector<Server> &servers){
	for (std::vector<Server>::iterator it = servers.begin(); it != servers.end(); ++it) {
		int servSocket = socket(AF_INET, SOCK_STREAM, 0);
		if(servSocket == -1)
			throw std::runtime_error("Error: socket creation failed");
		fcntl(servSocket, F_SETFL, O_NONBLOCK);
		it->setServSocket(servSocket);
		int optval = 1;
		if (setsockopt(it->getServSocket(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
			throw std::runtime_error("Error: setsockopt failed");
		sockaddr_in servAddress;
		servAddress.sin_family = AF_INET;
		servAddress.sin_addr.s_addr = it->getHost();
		servAddress.sin_port = htons(it->getPort());
		if (bind(it->getServSocket(), (sockaddr *)&servAddress, sizeof(servAddress)) == -1)
			throw std::runtime_error("Error: bind failed");
		if (listen(it->getServSocket(), 20) == -1)
			throw std::runtime_error("Error: listen failed");
		it->setServAddress(servAddress);
		pollfd fd = {it->getServSocket(), POLLIN, 0};
		fds.push_back(fd);
	}
}

// Getters
std::vector<Server> ManageServer::getServers() const{
	return servers;
}