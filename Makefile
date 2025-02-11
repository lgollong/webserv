CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -g -fsanitize=address

SRC_DIR = srcs
SRC = $(shell find $(SRC_DIR) -type f -name "*.cpp")
OBJ_DIR = objs
OBJ = $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

NAME = webserv

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