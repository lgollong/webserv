#include <csignal>
#include <iostream>
#include "../headers/types.hpp"
#include "../headers/StaticFile.hpp"
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
	const char *configPath = kDefaultConfigPath;
	if (argc == 2)
		configPath = argv[1];

	signal(SIGPIPE, SIG_IGN);

	try {
		Config config(configPath);
		Http http;
		Cgi cgi;
		StaticFile files;
		Worker worker(config, http, cgi, files);

		worker.run();
	}
	catch (const std::exception &e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}