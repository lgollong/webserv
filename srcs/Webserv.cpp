#include "../headers/Webserv.hpp"
#include "../headers/Server.hpp"
#include "../headers/ConfigFile.hpp"
#include "../headers/ConfigParser.hpp"

void	error_msg(std::string str){
	std::cerr << "Error: " << str << std::endl;
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		std::cout << "Start server with: ./webserv </path/to/configfile>" << std::endl;
		return (1);
	}
	else
	{
		ConfigFile configFile(argv[1]);
		Server server;
		try{
			configFile.isConfigFileValid();
			ConfigParser configParser;
			configParser.parseConfigFile(configFile.getConfigFile());
			std::vector<Server> servers = configParser.getServers();
			ManageServers serverManager(servers);
			serverManager.setupServers(servers);
			serverManager.runServer();
		}
		catch (std::exception &e){
			std::cerr << e.what() << std::endl;
		}
	}
	return (0);
}