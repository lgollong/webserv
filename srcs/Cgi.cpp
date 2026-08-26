#include "Cgi.hpp"
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <cerrno>

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

static std::vector<std::string> buildEnv(const Request &request, const Route &route) {
		std::vector<std::string> env;

		env.push_back("REQUEST_METHOD=" + request.method);
		env.push_back("SCRIPT_NAME=" + route.cgi_pass);
		env.push_back("QUERY_STRING=" + request.query);
		env.push_back("GATEWAY_INTERFACE=CGI/1.1");
		env.push_back("SERVER_PROTOCOL=HTTP/1.1");

		std::ostringstream len;
		len << request.body.size();
		env.push_back("CONTENT_LENGTH=" + len.str());

		std::map<std::string, std::string>::const_iterator it = request.headers.find("Content-Type");
		if (it != request.headers.end())
				env.push_back("CONTENT_TYPE=" + it->second);

		// every other request header, mapped Foo-Bar -> HTTP_FOO_BAR
		for (it = request.headers.begin(); it != request.headers.end(); ++it) {
				std::string key = "HTTP_" + it->first;
				for (std::string::size_type i = 0; i < key.size(); ++i) {
						if (key[i] == '-') key[i] = '_';
						else key[i] = static_cast<char>(std::toupper(key[i]));
				}
				env.push_back(key + "=" + it->second);
		}

		return env;
}

CgiJob Cgi::start(const Request &request, const Route &route) {
	int inPipe[2], outPipe[2];
	pipe(inPipe);
	pipe(outPipe);	
	CgiJob job;
	
	pid_t pid = fork();
	if (pid == 0) {
		dup2(inPipe[0], STDIN_FILENO);
		dup2(outPipe[1], STDOUT_FILENO);
		close(inPipe[0]);
		close(inPipe[1]);
		close(outPipe[0]);
		close(outPipe[1]);
		
		std::vector<std::string> envStrings = buildEnv(request, route);
		std::vector<char*> envp;
		for (size_t i = 0; i < envStrings.size(); ++i)
				envp.push_back(const_cast<char*>(envStrings[i].c_str()));
		envp.push_back(NULL);

		char *argv[] = { const_cast<char*>(route.cgi_pass.c_str()), NULL };

		execve(route.cgi_pass.c_str(), argv, &envp[0]);
		perror("execve"); // if execve fails, print reason
	}
	else {
		close(inPipe[0]);
		close(outPipe[1]);
		job.pid = pid;
		job.in_fd = inPipe[1];
		job.out_fd = outPipe[0];
		fcntl(job.in_fd, F_SETFL, O_NONBLOCK);
		fcntl(job.out_fd, F_SETFL, O_NONBLOCK);
	}
	return job;
}

// @todo waitpid missing
bool Cgi::collect(CgiJob &cgi) {
	char buf[4096];
	ssize_t n = read(cgi.out_fd, buf, sizeof(buf));

	if (n > 0) {
		cgi.output.append(buf, static_cast<size_t>(n));
		return false;
	}
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return false;
		cgi.done = true; // real read error, stop waiting, treat as complete
		return true;
	}
	cgi.done = true;
	return true;
}

bool Cgi::sendBody(CgiJob &job, const std::string &body) {
	if (job.sent >= body.size())
		return true;
	ssize_t n = write(job.in_fd, body.data() + job.sent, body.size() - job.sent);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return false;
		return true;
	}

	job.sent += n;
	return job.sent == body.size();
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
	if (raw.empty()) {
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
