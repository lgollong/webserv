#include "../headers/ConfigParser.hpp"

ConfigParser::ConfigParser(){

}

ConfigParser::~ConfigParser(){

}

std::string trim(const std::string &str) {
	size_t first = str.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	size_t last = str.find_last_not_of(" \t");
	return str.substr(first, (last - first + 1));
}

std::string cleanString(const std::string &str) {
	std::string result;
	for (size_t i = 0; i < str.size(); i++) {
		if (str[i] != ';' && str[i] != '{' && str[i] != '}')
			result += str[i];
	}
	return result;
}

void ConfigParser::parseConfigFile(std::string configFile){
	std::ifstream fileStream(configFile.c_str());
	if (!fileStream.is_open()) {
		throw std::runtime_error("Error: can not open file");
	}
	std::string line;
	std::string location;
	int serverCount = 1;
	while (std::getline(fileStream, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		line = trim(line);
		if (line.find('{') != std::string::npos && line.find("server") != std::string::npos) {
			Server server;
			while (std::getline(fileStream, line)) {
				if (line.empty() || line[0] == '#')
					continue;
				else if (line.find('}') != std::string::npos)
					break;
				line = trim(line);
				if (line.find('{') != std::string::npos && line.find("location") != std::string::npos) {
					std::istringstream iss(line);
					std::string locationKeyword, locationPath;
					if (iss >> locationKeyword >> locationPath){
						location = locationPath;
						location = cleanString(location);
						location = trim(location);
					}
					std::map<std::string, std::vector<std::string> > locationConfig;
					while (std::getline(fileStream, line)) {
						if (line.empty() || line[0] == '#')
							continue;
						if (line.find('}') != std::string::npos)
							break;
						line = trim(line);
						line = cleanString(line);
						std::istringstream iniss(line);
						std::string key, value;
						if (iniss >> key) {
							std::vector<std::string> values;
							while (iniss >> value) {
								values.push_back(value);
							}
							locationConfig[key] = values;
						}
					}
					server.setLocations(location, locationConfig);
				} else {
					line = cleanString(line);
					std::istringstream iss(line);
					std::string key, value;
					if (iss >> key >> value) {
						if (key == "server_name")
							server.setServerName(value);
						else if (key == "host")
							server.setHost(inet_addr(value.c_str()));
						else if (key == "listen")
							server.setPort(atoi(value.c_str()));
						else if (key == "root")
							server.setRoot(value);
						else if (key == "client_max_body_size")
							server.setClientMaxBodySize(atoi(value.c_str()));
						else if (key == "index")
							server.setIndex(value);
					}
				}
			}
			server.setServId(serverCount);
			servers.push_back(server);
			servers[serverCount - 1].printServerConfig();
			serverCount++;
		}
	}
	fileStream.close();
}

// Getters

std::vector<Server> ConfigParser::getServers() const{
	return servers;
}
