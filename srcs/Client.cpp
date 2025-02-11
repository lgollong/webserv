#include "../headers/Client.hpp"

Client::Client(){
	this->clientId = 0;
	this->clientSocket = 0;
}

Client::Client(int clientSocket, sockaddr_in clientAddress, int clientId){
	this->clientSocket = clientSocket;
	this->clientAddress = clientAddress;
	this->clientId = clientId;
}

Client::~Client(){

}

void	Client::printClientConfig(){
	std::cout << "----------------------------" << std::endl;
	std::cout << "Client id: " << clientId << std::endl;
	std::cout << "Client socket: " << clientSocket << std::endl;
	std::cout << "Client address: " << inet_ntoa(clientAddress.sin_addr) << std::endl;
	std::cout << "----------------------------" << std::endl;
}

void	Client::setClientSocket(int clientSocket){
	this->clientSocket = clientSocket;
}

void	Client::setClientAddress(sockaddr_in clientAddress){
	this->clientAddress = clientAddress;
}

void	Client::setClientId(int clientId){
	this->clientId = clientId;
}

int	Client::getClientSocket() const{
	return clientSocket;
}

sockaddr_in	Client::getClientAddress() const{
	return clientAddress;
}

int	Client::getClientId() const{
	return clientId;
}
