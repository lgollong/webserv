#include <iostream>
#include "../headers/StaticFile.hpp"
#include "../headers/Logger.hpp"
#include "../headers/Cgi.hpp"
#include "../headers/Http.hpp"
#include "../headers/Config.hpp"
#include "../headers/Worker.hpp"

int main(int argc, char *argv[]){
	(void)argc;
	(void)argv;

	Config config;
	Http http;
	Cgi cgi;
	StaticFile files;
	Logger logger(std::cout, std::cerr, 1);
	Worker worker(config, http, cgi, files, logger);

	worker.start();
	return (0);
}