#include <csignal>
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

	// Writing to a peer that has fully disconnected must fail with an error
	// result the event loop handles, not terminate the process.
	signal(SIGPIPE, SIG_IGN);

	// Errors that stop the server are always reported, whatever log_level says.
	try {
		Config config(configPath);
		Logger logger(std::cout, std::cerr, config.logLevel());
		Http http;
		Cgi cgi;
		StaticFile files;
		Worker worker(config, http, cgi, files, logger);

		worker.run();
	}
	catch (const std::exception &e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
		return (1);
	}
	return (0);
}