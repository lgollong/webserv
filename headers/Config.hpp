#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include "types.hpp"

class Config {
	public:
		Config(const std::string &configPath);
		~Config();

		Route                             route(size_t serverIndex, const Request &request) const;
		const std::vector<ServerConfig> & servers() const;
		std::string                       errorPage(size_t serverIndex, int status) const;
		bool                              debugLogs() const;

	private:
		std::vector<ServerConfig> server_configs;
		bool                      debug_logs;
};

#endif
