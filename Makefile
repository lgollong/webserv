CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -g -fsanitize=address -Iheaders

SRC_DIR = srcs
SRC = $(shell find $(SRC_DIR) -type f -name "*.cpp")
OBJ_DIR = objs
OBJ = $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

NAME = webserv
RESILIENCE_TEST = /private/tmp/webserv-resilience-tests
CONNECTION_LIFECYCLE_TEST = /private/tmp/webserv-connection-lifecycle-tests
CGI_PIPE_TEST = /private/tmp/webserv-cgi-pipe-tests
CONFIG_MODEL_TEST = /private/tmp/webserv-config-model-tests
STATIC_FILE_TEST = /private/tmp/webserv-static-file-tests
EVENT_LOOP_STRESS_TEST = /private/tmp/webserv-event-loop-stress-tests
CORE_SRC = $(filter-out $(SRC_DIR)/main.cpp,$(SRC))

all: $(NAME)

$(NAME): $(OBJ)
	@$(CXX) $(CXXFLAGS) $(OBJ) -o $(NAME)
	@echo "\033[1;32m ✅ [webserv created]\033[0m"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

clean:
	@rm -rf $(OBJ_DIR)
	@echo "\033[0;31m 🗑️  [objects deleted]\033[0m"

fclean: clean
	@rm -f $(NAME)
	@echo "\033[0;31m 🗑️  [webserv deleted]\033[0m"

re: fclean all

.PHONY: all clean fclean re resilience-test connection-lifecycle-test cgi-pipe-test config-model-test static-file-test event-loop-stress-test

resilience-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/resilience_tests.cpp -o $(RESILIENCE_TEST)
	@$(RESILIENCE_TEST)

connection-lifecycle-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/connection_lifecycle_tests.cpp -o $(CONNECTION_LIFECYCLE_TEST)
	@$(CONNECTION_LIFECYCLE_TEST)

cgi-pipe-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/cgi_pipe_tests.cpp -o $(CGI_PIPE_TEST)
	@$(CGI_PIPE_TEST)

config-model-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/config_model_tests.cpp srcs/Config.cpp -o $(CONFIG_MODEL_TEST)
	@$(CONFIG_MODEL_TEST)

static-file-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/static_file_tests.cpp srcs/StaticFile.cpp -o $(STATIC_FILE_TEST)
	@$(STATIC_FILE_TEST)

event-loop-stress-test: $(NAME)
	@$(CXX) $(CXXFLAGS) tests/event_loop_stress_tests.cpp $(CORE_SRC) -o $(EVENT_LOOP_STRESS_TEST)
	@$(EVENT_LOOP_STRESS_TEST)
