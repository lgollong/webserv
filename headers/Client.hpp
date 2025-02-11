#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "Webserv.hpp"

class Client {
	public:
		Client();
		Client(int clientSocket, sockaddr_in clientAddress, int clientId);
		~Client();

		void	setClientSocket(int clientSocket);
		void	setClientAddress(sockaddr_in clientAddress);
		void	setClientId(int clientId);
		void	printClientConfig();

		int			getClientSocket() const;
		sockaddr_in	getClientAddress() const;
		int			getClientId() const;

	private:
		int			clientSocket;
		sockaddr_in	clientAddress;
		int			clientId;
};

#endif