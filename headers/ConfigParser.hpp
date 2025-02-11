#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "Webserv.hpp"

class Server;

class ConfigParser {
public:
	ConfigParser();
	~ConfigParser();

	void parseConfigFile(std::string configFile);

	std::vector<Server> getServers() const;

private:
	std::vector<Server> servers;
};

#endif
