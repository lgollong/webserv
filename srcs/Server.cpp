#include "../headers/Server.hpp"

Server::Server(){
	this->servId = 0;
	this->host = INADDR_ANY;
	this->port = 0;
	this->clientMaxBodySize = MAX_CONTENT;
	this->serverName = "";
	this->root = "";
	this->servSocket = 0;
	this->index = "";
}

Server::~Server(){

}

void	Server::printServerConfig(){
	std::cout << "----------------------------" << std::endl;
	std::cout << "Server id: " << servId << std::endl;
	std::cout << "Server name: " << serverName << std::endl;
	std::cout << "Host: " << host << std::endl;
	std::cout << "Port: " << port << std::endl;
	std::cout << "Root: " << root << std::endl;
	std::cout << "Index: " << index << std::endl;
	std::cout << "Client max body size: " << clientMaxBodySize << std::endl;
	std::cout << "Error pages: " << std::endl;
	for (std::map<int, std::string>::iterator it = errorPages.begin(); it != errorPages.end(); it++)
		std::cout << "\tError: " << it->first << " Page: " << it->second << std::endl;
	std::cout << std::endl;
	std::cout << "Locations: " << std::endl;
	std::cout << "Number of locations: " << locations.size() << std::endl;
	for (std::vector<Location>::iterator it = locations.begin(); it != locations.end(); it++){
		std::cout << "Path: " << it->getPath() << std::endl;
		std::cout << "Methodes: " << std::endl;
		std::vector<std::string> methodes = it->getMethodes();
		for (std::vector<std::string>::iterator it2 = methodes.begin(); it2 != methodes.end(); it2++)
			std::cout << "\t" << *it2 << std::endl;
		std::cout << "Root: " << it->getRoot() << std::endl;
		std::cout << "Index: " << it->getIndex() << std::endl;
		std::cout << "Client max body size: " << it->getClientMaxBodySize() << std::endl;
		std::cout << "Autoindex: " << it->getAutoIndex() << std::endl;
		std::cout << "Return: " << it->getReturn() << std::endl;
		std::cout << "Cgi paths: " << std::endl;
		std::vector<std::string> cgiPaths = it->getCgiPaths();
		for (std::vector<std::string>::iterator it2 = cgiPaths.begin(); it2 != cgiPaths.end(); it2++)
			std::cout << "\t" << *it2 << std::endl;
		std::cout << "Cgi extensions: " << std::endl;
		std::vector<std::string> cgiExtensions = it->getCgiExtensions();
		for (std::vector<std::string>::iterator it2 = cgiExtensions.begin(); it2 != cgiExtensions.end(); it2++)
			std::cout << "\t" << *it2 << std::endl;
		std::cout << std::endl;

	}
}

// Setters

void	Server::setHost(in_addr_t host){
	this->host = host;
}

void	Server::setPort(uint16_t port){
	this->port = port;
}

void	Server::setServerName(std::string serverName){
	this->serverName = serverName;
}

void	Server::setRoot(std::string root){
	this->root = root;
}

void	Server::setClientMaxBodySize(unsigned long clientMaxBodySize){
	this->clientMaxBodySize = clientMaxBodySize;
}

void	Server::setServId(int servId){
	this->servId = servId;
}

void	Server::setIndex(std::string index){
	this->index = index;
}

void Server::setServSocket(int servSocket){
	this->servSocket = servSocket;
}

void Server::setServAddress(sockaddr_in servAddress){
	this->servAddress = servAddress;
}

void	Server::setLocations(Location locations){
	this->locations.push_back(locations);
}

void	Server::setErrorPages(int error, std::string page){
	errorPages[error] = page;
}

// Getters

int	Server::getServSocket() const{
	return servSocket;
}

int	Server::getServId() const{
	return servId;
}

in_addr_t	Server::getHost() const{
	return host;
}

uint16_t	Server::getPort() const{
	return port;
}

std::string	Server::getServerName() const{
	return serverName;
}

std::string	Server::getRoot() const{
	return root;
}

unsigned long	Server::getClientMaxBodySize() const{
	return clientMaxBodySize;
}

std::string	Server::getIndex() const{
	return index;
}

sockaddr_in	Server::getServAddress() const{
	return servAddress;
}

std::string	Server::getErrorPage(int error) const{
	std::map<int, std::string>::const_iterator it = errorPages.find(error);
	if (it != errorPages.end())
		return it->second;
	return "";
}

std::vector<Location>	Server::getLocations() const{
	return locations;
}