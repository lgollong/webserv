#pragma once
#ifndef WEBSERV_HPP
#define WEBSERV_HPP

#include <iostream>
#include <vector>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <limits>
#include <sys/stat.h>
#include <stdexcept>
#include <fstream>
#include <map>
#include <sstream>
#include <algorithm>
#include <arpa/inet.h>

#include "Server.hpp"
#include "ConfigFile.hpp"
#include "ConfigParser.hpp"
#include "ManageServer.hpp"
#include "Client.hpp"
#include "Location.hpp"

#define MAX_CONTENT 10000000

void	error_msg(std::string str);

#endif