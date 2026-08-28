CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -Iheaders

ifneq ($(SANITIZE),)
CXXFLAGS += -g -fsanitize=$(SANITIZE)
endif

SRC_DIR = srcs
SRC = $(shell find $(SRC_DIR) -type f -name "*.cpp")
OBJ_DIR = objs
OBJ = $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

NAME = webserv
RESILIENCE_TEST = /private/tmp/webserv-resilience-tests
CONNECTION_LIFECYCLE_TEST = /private/tmp/webserv-connection-lifecycle-tests
CGI_PIPE_TEST = /private/tmp/webserv-cgi-pipe-tests
CGI_LIFECYCLE_TEST = /private/tmp/webserv-cgi-lifecycle-tests
CGI_FIXTURE = contents/cgi/test.cgi
CONFIG_MODEL_TEST = /private/tmp/webserv-config-model-tests
STATIC_FILE_TEST = /private/tmp/webserv-static-file-tests
EVENT_LOOP_STRESS_TEST = /private/tmp/webserv-event-loop-stress-tests
SESSION_STORE_TEST = /private/tmp/webserv-session-store-tests
COOKIE_SESSION_TEST = /private/tmp/webserv-cookie-session-tests
CORE_SRC = $(filter-out $(SRC_DIR)/main.cpp,$(SRC))

all: $(NAME)

$(NAME): $(OBJ) $(CGI_FIXTURE)
	@$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)
	@echo "\033[1;32m ✅ [webserv created]\033[0m"

$(CGI_FIXTURE): tests/cgi_direct_fixture.cpp
	@$(CXX) $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

clean:
	@rm -rf $(OBJ_DIR)
	@echo "\033[0;31m 🗑️  [objects deleted]\033[0m"

fclean: clean
	@rm -rf $(NAME) $(CGI_FIXTURE) $(CGI_FIXTURE).dSYM
	@echo "\033[0;31m 🗑️  [webserv deleted]\033[0m"

re: fclean all

.PHONY: all clean fclean re resilience-test connection-lifecycle-test cgi-pipe-test cgi-lifecycle-test config-model-test static-file-test event-loop-stress-test session-store-test cookie-session-test

resilience-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/resilience_tests.cpp -o $(RESILIENCE_TEST)
	@$(RESILIENCE_TEST)

connection-lifecycle-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/connection_lifecycle_tests.cpp -o $(CONNECTION_LIFECYCLE_TEST)
	@$(CONNECTION_LIFECYCLE_TEST)

cgi-pipe-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/cgi_pipe_tests.cpp -o $(CGI_PIPE_TEST)
	@$(CGI_PIPE_TEST)

cgi-lifecycle-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/cgi_lifecycle_tests.cpp srcs/Cgi.cpp -o $(CGI_LIFECYCLE_TEST)
	@$(CGI_LIFECYCLE_TEST)

config-model-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/config_model_tests.cpp srcs/Config.cpp -o $(CONFIG_MODEL_TEST)
	@$(CONFIG_MODEL_TEST)

static-file-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/static_file_tests.cpp srcs/StaticFile.cpp -o $(STATIC_FILE_TEST)
	@$(STATIC_FILE_TEST)

event-loop-stress-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/event_loop_stress_tests.cpp $(CORE_SRC) -o $(EVENT_LOOP_STRESS_TEST)
	@$(EVENT_LOOP_STRESS_TEST)

session-store-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/session_store_tests.cpp srcs/Session.cpp -o $(SESSION_STORE_TEST)
	@$(SESSION_STORE_TEST)

cookie-session-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/cookie_session_tests.cpp -o $(COOKIE_SESSION_TEST)
	@$(COOKIE_SESSION_TEST)
