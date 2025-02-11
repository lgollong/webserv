#ifndef CONFIGFILE_HPP
#define CONFIGFILE_HPP

#include "Webserv.hpp"

class ConfigFile {
	public:
		ConfigFile(std::string input);
		~ConfigFile();

		void	isConfigFileValid();
		std::string	getConfigFile();
	private:
		std::string configFile;
};

#endif