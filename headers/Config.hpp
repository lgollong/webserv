#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include "types.hpp"

// @note are we following 42s cpp standards in the project? e.g. there is no default constructor here
// Parses the config file (startup); resolves a request to its location (per request).
class Config {
	public:
		Config(const std::string &configPath);
		~Config();

		Route route(const Request &request) const;
		const std::vector<ServerConfig>& servers() const;
		size_t bodyLimit() const;

	private:
		std::vector<ServerConfig> server_configs;

		void buildReferenceMock();
};

#endif
