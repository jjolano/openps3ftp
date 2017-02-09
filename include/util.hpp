/* util.hpp: FTP utilities class definition. */

#pragma once

#include "common.hpp"

namespace FTP
{
	namespace Utilities
	{
		std::string get_working_directory(std::vector<std::string> cwd_vector);
		void set_working_directory(std::vector<std::string>* cwd_vector, std::string new_path);
		std::string get_absolute_path(std::string old_path, std::string new_path);
		std::pair<std::string, std::string> parse_command_string(const char* to_parse);
		int parse_port_tuple(unsigned short tuple[6], const char* to_parse);
		bool get_file_mode(char mode[11], ftpstat stat);
		bool get_file_mode(char mode[11], std::string path);
		std::string string_to_upper(std::string str);
		bool file_exists(std::string path);
	};
};
