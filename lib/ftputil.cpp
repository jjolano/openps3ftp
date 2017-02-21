/* util.hpp: FTP utilities class. */

#include "util.hpp"

namespace FTP
{
	namespace Utilities
	{
		std::string get_working_directory(std::vector<std::string> cwd_vector)
		{
			using namespace std;
			string ret;
			vector<string>::iterator cwd_vector_it;

			for(cwd_vector_it = cwd_vector.begin(); cwd_vector_it != cwd_vector.end(); ++cwd_vector_it)
			{
				if(!(*cwd_vector_it).empty())
				{
					ret += '/';
					ret += *cwd_vector_it;
				}
			}

			if(ret.empty())
			{
				ret = '/';
			}

			return ret;
		}

		void set_working_directory(std::vector<std::string>* cwd_vector, std::string new_path)
		{
			cwd_vector->clear();

			std::stringstream ss_cwd(new_path);
			std::string dir;

			while(std::getline(ss_cwd, dir, '/'))
			{
				cwd_vector->push_back(dir);
			}
		}

		std::string get_absolute_path(std::string old_path, std::string new_path)
		{
			if(old_path.empty())
			{
				old_path = '/';
			}

			if(new_path.empty())
			{
				return old_path;
			}

			if(new_path[0] == '/')
			{
				return new_path;
			}

			if(new_path[new_path.size() - 1] == '/')
			{
				// remove trailing slash
				new_path.resize(new_path.size() - 1);
			}

			if(old_path[old_path.size() - 1] != '/')
			{
				// cwd must have trailing slash
				old_path += '/';
			}

			return old_path + new_path;
		}

		std::pair<std::string, std::string> parse_command_string(const char* to_parse)
		{
			std::string data(to_parse);
			std::stringstream parser(data);

			std::string name, params, param;

			parser >> name;

			while(parser >> param)
			{
				if(!params.empty())
				{
					params += ' ';
				}

				params += param;
			}

			name = string_to_upper(name);

			return std::make_pair(name, params);
		}

		int parse_port_tuple(unsigned short tuple[6], const char* to_parse)
		{
			return sscanf(to_parse, "%hu,%hu,%hu,%hu,%hu,%hu",
				&tuple[0],
				&tuple[1],
				&tuple[2],
				&tuple[3],
				&tuple[4],
				&tuple[5]
			);
		}

		bool get_file_mode(char mode[11], ftpstat stat)
		{
			mode[0] = '?';

			if((stat.st_mode & S_IFMT) == S_IFDIR)
			{
				mode[0] = 'd';
			}

			if((stat.st_mode & S_IFMT) == S_IFREG)
			{
				mode[0] = '-';
			}
			
			if((stat.st_mode & S_IFMT) == S_IFLNK)
			{
				mode[0] = 'l';
			}

			mode[1] = ((stat.st_mode & S_IRUSR) ? 'r' : '-');
			mode[2] = ((stat.st_mode & S_IWUSR) ? 'w' : '-');
			mode[3] = ((stat.st_mode & S_IXUSR) ? 'x' : '-');
			mode[4] = ((stat.st_mode & S_IRGRP) ? 'r' : '-');
			mode[5] = ((stat.st_mode & S_IWGRP) ? 'w' : '-');
			mode[6] = ((stat.st_mode & S_IXGRP) ? 'x' : '-');
			mode[7] = ((stat.st_mode & S_IROTH) ? 'r' : '-');
			mode[8] = ((stat.st_mode & S_IWOTH) ? 'w' : '-');
			mode[9] = ((stat.st_mode & S_IXOTH) ? 'x' : '-');
			mode[10] = '\0';

			return true;
		}

		bool get_file_mode(char mode[11], std::string path)
		{
			ftpstat stat;

			if(FTP::IO::stat(path, &stat) != 0)
			{
				return false;
			}

			return get_file_mode(mode, stat);
		}

		std::string string_to_upper(std::string str)
		{
			//std::transform(str.begin(), str.end(), str.begin(), ::toupper);
			
			std::string ret;
			const char* s = str.c_str();

			int c = 0;
 
			while(s[c] != '\0')
			{
				if (s[c] >= 'a' && s[c] <= 'z')
				{
					ret += (char) (s[c] - 32);
				}
				else
				{
					ret += (char) (s[c]);
				}

				c++;
			}

			return ret;
		}

		bool file_exists(std::string path)
		{
			ftpstat stat;
			return (FTP::IO::stat(path, &stat) == 0);
		}
	};
};
