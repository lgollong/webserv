#include "../headers/Server.hpp"

Server::Server(){
	this->servId = 0;
	this->host = INADDR_ANY;
	this->port = 0;
	this->client_max_body_size = MAX_CONTENT;
	this->server_name = "";
	this->root = "";
	this->servSocket = 0;
	this->index = "";
}

Server::~Server(){

}

void	Server::printServerConfig(){
	std::cout << "----------------------------" << std::endl;
	std::cout << "Server id: " << servId << std::endl;
	std::cout << "Server name: " << server_name << std::endl;
	std::cout << "Host: " << host << std::endl;
	std::cout << "Port: " << port << std::endl;
	std::cout << "Root: " << root << std::endl;
	std::cout << "Index: " << index << std::endl;
	std::cout << "Client max body size: " << client_max_body_size << std::endl << std::endl;
	std::cout << "Locations: " << std::endl;
	std::cout << "Number of locations: " << locations.size() << std::endl;
	for (size_t i = 0; i < locations.size(); i++){
		std::cout << "Location path: " << locations[i].path << std::endl;
		std::cout << "Location options: " << std::endl;
		std::map<std::string, std::vector<std::string> > options = locations[i].options;
		for (std::map<std::string, std::vector<std::string> >::iterator it = options.begin(); it != options.end(); it++){
			std::cout << "\t" << it->first << ": ";
			for (size_t j = 0; j < it->second.size(); j++){
				std::cout << it->second[j] << " ";
			}
			std::cout << std::endl;
		}
	}
}

// Setters

void	Server::setHost(in_addr_t host){
	this->host = host;
}

void	Server::setPort(uint16_t port){
	this->port = port;
}

void	Server::setServerName(std::string server_name){
	this->server_name = server_name;
}

void	Server::setRoot(std::string root){
	this->root = root;
}

void	Server::setLocations(std::string locationPath, std::map<std::string, std::vector<std::string> > locationConfig){
	Location location;
	location.path = locationPath;
	location.options = locationConfig;
	locations.push_back(location);
}

void	Server::setClientMaxBodySize(unsigned long client_max_body_size){
	this->client_max_body_size = client_max_body_size;
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
	return server_name;
}

std::string	Server::getRoot() const{
	return root;
}

std::vector<Location>	Server::getLocations() const{
	return locations;
}

unsigned long	Server::getClientMaxBodySize() const{
	return client_max_body_size;
}

std::string	Server::getIndex() const{
	return index;
}

sockaddr_in	Server::getServAddress() const{
	return servAddress;
}
