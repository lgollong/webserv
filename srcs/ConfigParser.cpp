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
	if (!fileStream.is_open())
		throw std::runtime_error("Error: can not open file");
	std::string line;
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
					Location location;
					std::string locationKey, locationPath;
					if (std::istringstream(line) >> locationKey >> locationPath)
						location.setPath(trim(cleanString(locationPath)));
					while (std::getline(fileStream, line)) {
						if (line.empty() || line[0] == '#')
							continue;
						if (line.find('}') != std::string::npos)
							break;
						line = trim(line);
						line = cleanString(line);
						std::istringstream iss(line);
						std::string key, value;
						if (iss >> key) {
							if (key == "allow_methods") {
								while (iss >> value)
									location.setMethodes(value);
							}
							else if (key == "root" && iss >> value)
								location.setRoot(value);
							else if (key == "index" && iss >> value)
								location.setIndex(value);
							else if (key == "client_max_body_size" && iss >> value)
								location.setClientMaxBodySize(atoi(value.c_str()));
							else if (key == "autoindex" && iss >> value)
								location.setAutoIndex(value);
							else if (key == "return" && iss >> value)
								location.setReturn(value);
							else if (key == "cgi_path") {
								while (iss >> value)
									location.setCgiPath(value);
							}
							else if (key == "cgi_ext") {
								while (iss >> value)
									location.setCgiExtension(value);
							}
						}
					}
					server.setLocations(location);
				} else {
					line = cleanString(line);
					std::istringstream iss(line);
					std::string key, value;
					if (iss >> key) {
						if (key == "server_name" && iss >> value)
							server.setServerName(value);
						else if (key == "host" && iss >> value)
							server.setHost(inet_addr(value.c_str()));
						else if (key == "listen" && iss >> value)
							server.setPort(atoi(value.c_str()));
						else if (key == "root" && iss >> value)
							server.setRoot(value);
						else if (key == "client_max_body_size" && iss >> value)
							server.setClientMaxBodySize(atoi(value.c_str()));
						else if (key == "index" && iss >> value)
							server.setIndex(value);
						else if (key == "error_page") {
							if (iss >> value) {
								int error = atoi(value.c_str());
								if (iss >> value)
									server.setErrorPages(error, value);
							}
						}
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
