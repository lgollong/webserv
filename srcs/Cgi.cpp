#include "Cgi.hpp"
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <ctime>
#include <cerrno>
#include <csignal>
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

//   Response (document response, or client redirect via Location; see buildResponse)

//   CGI-Response = 1*CGI-field *other-field NL response-body   ; Content-Type or Location required

//   Content-Type   = "Content-Type:" media-type NL
//   Location       = "Location:" URI NL                         ; without Status -> 302
//   Status         = "Status:" status-code [ SP reason-phrase ] NL
//   status-code    = 3digit                                    ; 200-599, known to Http
//   other-field    = field-name ":" field-value NL
//   response-body  = *OCTET

//   field-name      = token
//   field-value     = *( field-content | LWSP )

Cgi::Cgi() {}

Cgi::~Cgi() {}

// The subject permits fcntl() only with F_SETFL and O_NONBLOCK, so the
// descriptor flags are replaced rather than read and extended.
static bool setNonBlocking(int fd) {
	return fcntl(fd, F_SETFL, O_NONBLOCK) == 0;
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

// scriptFile is the script's path as seen from the child's working
// directory (its own directory), which is what interpreters such as
// php-cgi open through SCRIPT_FILENAME.
static std::vector<std::string> buildEnv(const Request &request, const Route &route,
	const ServerConfig &server, const std::string &remoteAddr, const std::string &scriptFile) {
	std::vector<std::string> env;

	env.push_back("REQUEST_METHOD=" + request.method);
	env.push_back("SCRIPT_NAME=" + route.cgi_script_name);
	env.push_back("SCRIPT_FILENAME=" + scriptFile);
	env.push_back("REQUEST_URI=" + request.path + (request.query.empty() ? "" : "?" + request.query));
	env.push_back("REMOTE_ADDR=" + remoteAddr);
	env.push_back("REDIRECT_STATUS=200");
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

// Config::route() builds cgi_script_path from the route root and rejects
// "." and ".." segments, so containment is checked lexically there.
static bool resolveScriptPath(const Route &route, std::string &path) {
	struct stat info;
	if (route.root.empty() || route.cgi_script_path.empty() ||
		stat(route.cgi_script_path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
		return false;
	path = route.cgi_script_path;
	return true;
}

static std::string baseNameOf(const std::string &path) {
	std::string::size_type slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}

static std::string directoryOf(const std::string &path) {
	std::string::size_type slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return ".";
	if (slash == 0)
		return "/";
	return path.substr(0, slash);
}

CgiJob Cgi::start(const Request &request, const Route &route, const ServerConfig &server,
	const std::string &remoteAddr, const std::vector<int> &serverFds) {
	CgiJob job;
	std::string scriptPath;
	// Handlers must be absolute: the child changes into the script directory
	// before execve(), which would break a handler path relative to the server.
	if (!resolveScriptPath(route, scriptPath) ||
		(!route.cgi_handler.empty() && route.cgi_handler[0] != '/')) {
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
			std::exit(127);
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);
		for (std::vector<int>::const_iterator fd = serverFds.begin(); fd != serverFds.end(); ++fd) {
			if (*fd > STDERR_FILENO)
				close(*fd);
		}
		
		if (chdir(directoryOf(scriptPath).c_str()) != 0)
			std::exit(127);
		// After chdir() the script is addressed relative to its own directory.
		const std::string scriptFile = baseNameOf(scriptPath);
		const std::string directScript = "./" + scriptFile;

		std::vector<std::string> envStrings = buildEnv(request, route, server, remoteAddr, scriptFile);
		std::vector<char*> envp;
		for (size_t i = 0; i < envStrings.size(); ++i)
				envp.push_back(const_cast<char*>(envStrings[i].c_str()));
		envp.push_back(NULL);

		std::vector<char*> argv;
		const std::string &program = route.cgi_handler.empty() ? directScript : route.cgi_handler;
		argv.push_back(const_cast<char*>(program.c_str()));
		if (!route.cgi_handler.empty())
			argv.push_back(const_cast<char*>(scriptFile.c_str()));
		argv.push_back(NULL);

		execve(program.c_str(), &argv[0], &envp[0]);
		std::exit(127);
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
		if (cgi.output.size() + static_cast<size_t>(n) > MAX_OUTPUT_BYTES) {
			cgi.failed = true;
			cgi.done = true;
			return true;
		}
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

static std::string lowerCase(const std::string &value) {
	std::string lowered(value);
	for (std::string::size_type i = 0; i < lowered.size(); ++i)
		lowered[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
	return lowered;
}

static bool isTokenChar(char value) {
	unsigned char c = static_cast<unsigned char>(value);
	return std::isalnum(c) || (value != '\0' && std::strchr("!#$%&'*+-.^_`|~", value) != NULL);
}

static bool isFieldName(const std::string &name) {
	if (name.empty())
		return false;
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		if (!isTokenChar(name[i]))
			return false;
	}
	return true;
}

static bool isFieldValue(const std::string &value) {
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(value[i]);
		if (c != '\t' && (c < 0x20 || c == 0x7f))
			return false;
	}
	return true;
}

static std::string trimWhitespace(const std::string &value) {
	std::string::size_type first = value.find_first_not_of(" \t");
	if (first == std::string::npos)
		return "";
	return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

// Status = "Status:" status-code [ SP reason-phrase ]; only final statuses.
static bool parseStatus(const std::string &value, int &status) {
	if (value.size() < 3 || (value.size() > 3 && value[3] != ' '))
		return false;
	for (int i = 0; i < 3; ++i) {
		if (!std::isdigit(static_cast<unsigned char>(value[i])))
			return false;
	}
	status = (value[0] - '0') * 100 + (value[1] - '0') * 10 + (value[2] - '0');
	return status >= 200 && status <= 599;
}

// The server frames the client response itself, so these never pass through.
static bool isServerControlledField(const std::string &lowerName) {
	return lowerName == "content-length" || lowerName == "transfer-encoding" ||
		lowerName == "connection" || lowerName == "keep-alive" || lowerName == "upgrade" ||
		lowerName == "proxy-connection" || lowerName == "te" || lowerName == "trailer";
}

static Response badGateway() {
	Response response;
	response.status = 502;
	return response;
}

// CGI-Response = *( header-field NL ) NL response-body   (RFC 3875 section 6)
// Header lines end in LF or CRLF; the first empty line ends the header block.
// Content-Type, Status, and Location are matched case-insensitively and may
// appear once. A document response needs Content-Type; a redirect needs
// Location and defaults to 302. Anything else malformed is a 502.
Response Cgi::buildResponse(const CgiJob &job) const {
	const std::string &raw = job.output;
	if (job.failed || raw.empty())
		return badGateway();

	Response response;
	bool hasStatus = false;
	bool hasContentType = false;
	bool hasLocation = false;
	std::string::size_type pos = 0;
	while (true) {
		std::string::size_type end = raw.find('\n', pos);
		if (end == std::string::npos)
			return badGateway(); // no empty line terminates the headers
		std::string line = raw.substr(pos, end - pos);
		pos = end + 1;
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			break;

		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos || !isFieldName(line.substr(0, colon)))
			return badGateway();
		const std::string name = line.substr(0, colon);
		const std::string value = trimWhitespace(line.substr(colon + 1));
		if (!isFieldValue(value))
			return badGateway();

		const std::string lowerName = lowerCase(name);
		if (lowerName == "status") {
			if (hasStatus || !parseStatus(value, response.status))
				return badGateway();
			hasStatus = true;
		} else if (lowerName == "content-type") {
			if (hasContentType || value.empty())
				return badGateway();
			response.headers["Content-Type"] = value;
			hasContentType = true;
		} else if (lowerName == "location") {
			if (hasLocation || value.empty())
				return badGateway();
			response.headers["Location"] = value;
			hasLocation = true;
		} else if (!isServerControlledField(lowerName)) {
			response.headers[name] = value;
		}
	}

	if (!hasContentType && !hasLocation)
		return badGateway();
	if (!hasStatus)
		response.status = hasLocation ? 302 : 200;
	response.body = raw.substr(pos);
	return response;
}
