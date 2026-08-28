#include "Cgi.hpp"
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <ctime>
#include <cerrno>
#include <csignal>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>

// augmented BNF for CGI:
//   Meta-Variables (request → env)

//   meta-variable-name = "CONTENT_LENGTH" | "CONTENT_TYPE" |
//                         "GATEWAY_INTERFACE" | "PATH_INFO" |
//                         "QUERY_STRING" | "REQUEST_METHOD" |
//                         "SCRIPT_NAME" | "SERVER_NAME" |
//                         "SERVER_PORT" | "SERVER_PROTOCOL" |
//                         http-var-name
//   http-var-name    = "HTTP_" 1*( alphanum | "_" )   ; one per request header

//   CONTENT_LENGTH     = "" | 1*digit
//   CONTENT_TYPE       = "" | media-type
//   media-type         = token "/" token *( ";" attribute "=" value )
//   GATEWAY_INTERFACE  = "CGI/1.1"
//   PATH_INFO          = "" | ( "/" path )
//   QUERY_STRING       = *uric
//   REQUEST_METHOD     = "GET" | "POST" | "DELETE"
//   SCRIPT_NAME        = "/" path
//   SERVER_PROTOCOL    = "HTTP" "/" 1*digit "." 1*digit

//   Request body

//   Request-Data = <CONTENT_LENGTH>OCTET   ; exactly Content-Length bytes, already de-chunked by Http

//   Response (document-response only — no Location/redirects)

//   CGI-Response = Content-Type [ Status ] *other-field NL response-body

//   Content-Type   = "Content-Type:" media-type NL
//   Status         = "Status:" status-code SP reason-phrase NL
//   status-code    = 3digit
//   other-field    = field-name ":" field-value NL
//   response-body  = *OCTET

//   field-name      = token
//   field-value     = *( field-content | LWSP )

Cgi::Cgi() {}

Cgi::~Cgi() {}

static bool setNonBlocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return false;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void closePipe(int pipe_fds[2]) {
	close(pipe_fds[0]);
	close(pipe_fds[1]);
}

static std::string pathInfo(const Request &request, const Route &route) {
	std::string scriptName = route.cgi_script_name.empty() ? request.path : route.cgi_script_name;
	if (request.path.compare(0, scriptName.size(), scriptName) != 0)
		return "";
	return request.path.substr(scriptName.size());
}

static std::vector<std::string> buildEnv(const Request &request, const Route &route,
	const ServerConfig &server) {
	std::vector<std::string> env;

	env.push_back("REQUEST_METHOD=" + request.method);
	env.push_back("SCRIPT_NAME=" + route.cgi_script_name);
	env.push_back("PATH_INFO=" + pathInfo(request, route));
	env.push_back("QUERY_STRING=" + request.query);
	env.push_back("GATEWAY_INTERFACE=CGI/1.1");
	env.push_back("SERVER_PROTOCOL=" + request.version);
	env.push_back("SERVER_NAME=" + server.server_name);

	std::ostringstream port;
	port << server.port;
	env.push_back("SERVER_PORT=" + port.str());

	std::ostringstream len;
	len << request.body.size();
	env.push_back("CONTENT_LENGTH=" + len.str());

	std::map<std::string, std::string>::const_iterator it = request.headers.find("content-type");
	if (it != request.headers.end())
		env.push_back("CONTENT_TYPE=" + it->second);

	// Every other request header, mapped Foo-Bar -> HTTP_FOO_BAR.
	for (it = request.headers.begin(); it != request.headers.end(); ++it) {
		if (it->first == "content-type")
			continue;
		std::string key = "HTTP_" + it->first;
		for (std::string::size_type i = 0; i < key.size(); ++i) {
			if (key[i] == '-')
				key[i] = '_';
			else
				key[i] = static_cast<char>(std::toupper(key[i]));
		}
		env.push_back(key + "=" + it->second);
	}

	return env;
}

static bool resolveExecutablePath(const std::string &configuredPath, std::string &path) {
	if (configuredPath.empty())
		return false;
	if (configuredPath[0] == '/') {
		path = configuredPath;
		return true;
	}
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return false;
	path = std::string(cwd) + "/" + configuredPath;
	return true;
}

static bool isPathInsideRoot(const std::string &path, const std::string &root) {
	if (path.compare(0, root.size(), root) != 0)
		return false;
	return path.size() == root.size() || root[root.size() - 1] == '/' ||
		path[root.size()] == '/';
}

static bool resolveScriptPath(const Route &route, std::string &path) {
	char root[PATH_MAX];
	char script[PATH_MAX];
	struct stat info;
	if (route.root.empty() || route.cgi_script_path.empty() ||
		realpath(route.root.c_str(), root) == NULL ||
		realpath(route.cgi_script_path.c_str(), script) == NULL ||
		!isPathInsideRoot(script, root) || stat(script, &info) != 0 || !S_ISREG(info.st_mode))
		return false;
	path = script;
	return true;
}

static std::string directoryOf(const std::string &path) {
	std::string::size_type slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return path.substr(0, slash);
}

CgiJob Cgi::start(const Request &request, const Route &route, const ServerConfig &server) {
	CgiJob job;
	std::string handlerPath;
	std::string scriptPath;
	if (!resolveScriptPath(route, scriptPath) ||
		(!route.cgi_handler.empty() && !resolveExecutablePath(route.cgi_handler, handlerPath))) {
		job.failed = true;
		return job;
	}
	int inPipe[2] = {-1, -1};
	int outPipe[2] = {-1, -1};
	if (pipe(inPipe) < 0) {
		job.failed = true;
		return job;
	}
	if (pipe(outPipe) < 0) {
		closePipe(inPipe);
		job.failed = true;
		return job;
	}
	
	pid_t pid = fork();
	if (pid == 0) {
		if (dup2(inPipe[0], STDIN_FILENO) < 0 || dup2(outPipe[1], STDOUT_FILENO) < 0)
			_exit(127);
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);
		
		if (chdir(directoryOf(scriptPath).c_str()) != 0)
			_exit(127);

		std::vector<std::string> envStrings = buildEnv(request, route, server);
		std::vector<char*> envp;
		for (size_t i = 0; i < envStrings.size(); ++i)
				envp.push_back(const_cast<char*>(envStrings[i].c_str()));
		envp.push_back(NULL);

		std::vector<char*> argv;
		const std::string &program = route.cgi_handler.empty() ? scriptPath : handlerPath;
		argv.push_back(const_cast<char*>(program.c_str()));
		if (!route.cgi_handler.empty())
			argv.push_back(const_cast<char*>(scriptPath.c_str()));
		argv.push_back(NULL);

		execve(program.c_str(), &argv[0], &envp[0]);
		_exit(127);
	}
	if (pid < 0) {
		closePipe(inPipe);
		closePipe(outPipe);
		job.failed = true;
		return job;
	}

	close(inPipe[0]);
	close(outPipe[1]);
	job.pid = pid;
	job.started_at = time(NULL);
	job.last_activity = job.started_at;
	job.in_fd = inPipe[1];
	job.out_fd = outPipe[0];
	if (!setNonBlocking(job.in_fd) || !setNonBlocking(job.out_fd)) {
		close(job.in_fd);
		close(job.out_fd);
		job.in_fd = -1;
		job.out_fd = -1;
		job.failed = true;
	}
	return job;
}

bool Cgi::collect(CgiJob &cgi) {
	char buf[4096];
	ssize_t n = read(cgi.out_fd, buf, sizeof(buf));

	if (n > 0) {
		cgi.output.append(buf, static_cast<size_t>(n));
		cgi.last_activity = time(NULL);
		return false;
	}
	if (n < 0) {
		cgi.failed = true;
		cgi.done = true;
		return true;
	}
	cgi.done = true;
	return true;
}

bool Cgi::sendBody(CgiJob &job, const std::string &body) {
	if (job.sent >= body.size())
		return true;
	ssize_t n = write(job.in_fd, body.data() + job.sent, body.size() - job.sent);
	if (n <= 0) {
		job.failed = true;
		return true;
	}

	job.sent += n;
	job.last_activity = time(NULL);
	return job.sent == body.size();
}

void Cgi::terminate(CgiJob &job) const {
	if (job.pid <= 0 || job.termination_requested)
		return;
	kill(job.pid, SIGTERM);
	job.termination_requested = true;
	job.termination_requested_at = time(NULL);
}

void Cgi::forceTerminate(CgiJob &job) const {
	forceTerminate(job.pid);
}

void Cgi::forceTerminate(pid_t pid) const {
	if (pid > 0)
		kill(pid, SIGKILL);
}

bool Cgi::reap(CgiJob &job) const {
	return reap(job.pid);
}

bool Cgi::reap(pid_t &pid) const {
	if (pid <= 0)
		return true;

	int status = 0;
	pid_t result = waitpid(pid, &status, WNOHANG);
	if (result == 0)
		return false;
	if (result < 0 && errno == EINTR)
		return false;
	pid = -1;
	return true;
}

// header-field = CGI-field | other-field
// generic-field = field-name ":" [ field-value ] NL
static std::map<std::string, std::string> parseHeaderFields(const std::string &header_block) {
	std::map<std::string, std::string> fields;
	size_t line_start = 0;

	while (line_start < header_block.size()) {
		size_t line_end = header_block.find('\n', line_start);

		std::string line;
		if (line_end == std::string::npos)
			line = header_block.substr(line_start);
		else
			line = header_block.substr(line_start, line_end - line_start);

		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);

		std::string::size_type colon = line.find(':');
		if (colon != std::string::npos) {
			std::string field_name = line.substr(0, colon);
			std::string::size_type value_start = line.find_first_not_of(' ', colon + 1);
			std::string field_value;
			if (value_start == std::string::npos)
				fields[field_name] = "";
			else
				fields[field_name] = line.substr(value_start);
		}

		if (line_end == std::string::npos)
			break;
		line_start = line_end + 1;
	}

	return fields;
}

// CGI-Response = Content-Type [ Status ] *other-field NL response-body
Response Cgi::buildResponse(const CgiJob &job) const {
	const std::string &raw = job.output;

	// expect non-empty job output
	Response response;
	if (job.failed || raw.empty()) {
		response.status = 502; // empty output is not a valid cgi response
		return response;
	}

	std::string::size_type sep = raw.find("\r\n\r\n");
	size_t sep_len = 4;
	if (sep == std::string::npos) {
		sep = raw.find("\n\n");
		sep_len = 2;
	}

	std::string header_block = (sep == std::string::npos) ? "" : raw.substr(0, sep);
	std::map<std::string, std::string> fields = parseHeaderFields(header_block);

	// get content type
	std::map<std::string, std::string>::iterator content_type = fields.find("Content-Type");
	if (content_type != fields.end()) {
		response.headers["Content-Type"] = content_type->second;
		fields.erase(content_type);
	}

	// get status or create with 200
	std::map<std::string, std::string>::iterator status = fields.find("Status");
	if (status != fields.end()) {
		response.status = std::atoi(status->second.c_str());
		fields.erase(status);
	} else {
		response.status = 200;
	}

	// get variable amount of other-field
	for (std::map<std::string, std::string>::const_iterator it = fields.begin(); it != fields.end(); ++it)
		response.headers[it->first] = it->second;

	// get response body
	response.body = (sep == std::string::npos) ? raw : raw.substr(sep + sep_len);

	// return response object
	return response;
}
