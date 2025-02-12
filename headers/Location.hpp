#ifndef LOCATION_HPP
#define LOCATION_HPP

#include "Webserv.hpp"

class Location {
	public:
		Location();
		~Location();

		void	setPath(std::string path);
		void	setMethodes(std::string methode);
		void	setRoot(std::string root);
		void	setIndex(std::string index);
		void	setClientMaxBodySize(unsigned long clientMaxBodySize);
		void	setAutoIndex(std::string autoIndex);
		void	setReturn(std::string _return);
		void	setCgiPath(std::string cgiPath);
		void	setCgiExtension(std::string cgiExtension);

		std::string					getPath() const;
		std::string					getMethode(std::string methode) const;
		std::vector<std::string>	getMethodes() const;
		std::string					getRoot() const;
		std::string					getIndex() const;
		unsigned long				getClientMaxBodySize() const;
		bool						getAutoIndex() const;
		std::string					getReturn() const;
		std::string					getCgiPath(std::string path) const;
		std::vector<std::string>	getCgiPaths() const;
		std::string					getCgiExtension(std::string extension) const;
		std::vector<std::string>	getCgiExtensions() const;
	private:
		std::string					path;
		std::vector<std::string>	methodes;
		std::string					root;
		std::string					index;
		unsigned long				clientMaxBodySize;
		bool						autoIndex;
		std::string					_return;
		std::vector<std::string>	cgiPath;
		std::vector<std::string>	cgiExtension;
};

#endif