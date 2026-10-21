#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include "types.hpp"

// Parses the config file (startup); resolves a request to its location (per request).
class Config {
	public:
		Config(const std::string &configPath);
		~Config();

		Route                             route(const Request &request) const;
		Route                             route(size_t serverIndex, const Request &request) const;
		const std::vector<ServerConfig> & servers() const;
		std::string                       errorPage(size_t serverIndex, int status) const;
		size_t                            bodyLimit(size_t serverIndex) const;

	private:
		std::vector<ServerConfig> 	server_configs;

		std::string					parseLocation(ServerConfig &server, std::vector<std::string> &tokens,std::size_t &index, const std::string &configPath, std::vector<size_t> &lines);
		void 						parseFile(const std::string &configPath);
		void 						parseServer(std::vector<std::string> &tokens, std::size_t &index, const std::string &configPath, std::vector<size_t> &lines);
		std::vector<std::string> 	tokenize(const std::string &configPath, std::vector<size_t> &lines) const;
		std::string 				readFile(const std::string &configPath) const;
		int 						parsePort(const std::string &value, const std::string &configPath, std::size_t line) const;
		size_t 						parseBodySize(const std::string &value, const std::string &configPath, std::size_t line) const;
		void 						expectToken(const std::string &expected, std::vector<std::string> &tokens, std::size_t &index, const std::string &configPath, const std::vector<size_t> &lines) const;
		void 						expectSemicolon(std::vector<std::string> &tokens, std::size_t &index, const std::string &configPath, const std::vector<size_t> &lines) const;
		void 						validateListen(const std::string &host, int port, const std::string &configPath, std::size_t line) const;
};

#endif
