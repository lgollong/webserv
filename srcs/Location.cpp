#include "../headers/Location.hpp"

Location::Location(){
	this->path = "";
	this->root = "";
	this->index = "";
	this->clientMaxBodySize = MAX_CONTENT;
	this->autoIndex = false;
	this->_return = "";
}

Location::~Location(){

}

void	Location::setPath(std::string path){
	this->path = path;
}

void	Location::setMethodes(std::string methode){
	this->methodes.push_back(methode);
}

void	Location::setRoot(std::string root){
	this->root = root;
}

void	Location::setIndex(std::string index){
	this->index = index;
}

void	Location::setClientMaxBodySize(unsigned long clientMaxBodySize){
	this->clientMaxBodySize = clientMaxBodySize;
}

void	Location::setAutoIndex(std::string autoIndex){
	if (autoIndex == "on")
		this->autoIndex = true;
	else
		this->autoIndex = false;
}

void	Location::setReturn(std::string _return){
	this->_return = _return;
}

void	Location::setCgiPath(std::string cgiPath){
	this->cgiPath.push_back(cgiPath);
}

void	Location::setCgiExtension(std::string cgiExtension){
	this->cgiExtension.push_back(cgiExtension);
}

std::string	Location::getPath() const{
	return path;
}

std::string	Location::getMethode(std::string methode) const{
	for (std::vector<std::string>::const_iterator it = methodes.begin(); it != methodes.end(); ++it){
		if (*it == methode)
			return *it;
	}
	return "";
}

std::vector<std::string>	Location::getMethodes() const{
	return methodes;
}

std::string	Location::getRoot() const{
	return root;
}

std::string	Location::getIndex() const{
	return index;
}

unsigned long	Location::getClientMaxBodySize() const{
	return clientMaxBodySize;
}

bool	Location::getAutoIndex() const{
	return autoIndex;
}

std::string	Location::getReturn() const{
	return _return;
}

std::string	Location::getCgiPath(std::string path) const{
	for (std::vector<std::string>::const_iterator it = cgiPath.begin(); it != cgiPath.end(); ++it){
		if (*it == path)
			return *it;
	}
	return "";
}

std::vector<std::string>	Location::getCgiPaths() const{
	return cgiPath;
}

std::string	Location::getCgiExtension(std::string extension) const{
	for (std::vector<std::string>::const_iterator it = cgiExtension.begin(); it != cgiExtension.end(); ++it){
		if (*it == extension)
			return *it;
	}
	return "";
}

std::vector<std::string>	Location::getCgiExtensions() const{
	return cgiExtension;
}

