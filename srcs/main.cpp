#include <iostream>
#include "../headers/types.hpp"
#include "../headers/StaticFile.hpp"
#include "../headers/Logger.hpp"
#include "../headers/Cgi.hpp"
#include "../headers/Http.hpp"
#include "../headers/Config.hpp"
#include "../headers/Worker.hpp"

static const char kDefaultConfigPath[] = "config/default.config";

int main(int argc, char *argv[]){
	if (argc > 2) {
		std::cerr << "Usage: " << argv[0] << " [configuration file]" << std::endl;
		return (1);
	}
	const char *configPath = (argc == 2) ? argv[1] : kDefaultConfigPath;

	Logger logger(std::cout, std::cerr, DEBUG);

	try {
		Config config(configPath);
		Http http;
		Cgi cgi;
		StaticFile files;
		Worker worker(config, http, cgi, files, logger);

		worker.run();
	}
	catch (const std::exception &e) {
		logger.error(e.what());
		return (1);
	}
	return (0);
}