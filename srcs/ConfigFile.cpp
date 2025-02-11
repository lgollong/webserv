#include "../headers/Webserv.hpp"

ConfigFile::ConfigFile(std::string input){
	configFile = input;
}

ConfigFile::~ConfigFile(){

}

void ConfigFile::isConfigFileValid() {
	struct stat buffer;
	if (stat(configFile.c_str(), &buffer) != 0)
		throw std::runtime_error("Error: file does not exist");
	if (access(configFile.c_str(), R_OK) != 0)
		throw std::runtime_error("Error: file is not readable");
}

std::string ConfigFile::getConfigFile(){
	return configFile;
}