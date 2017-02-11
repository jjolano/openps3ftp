/* base.cpp: Base FTP commands. */

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"
#include "site.hpp"

namespace feat
{
	namespace base
	{
		namespace data
		{
			bool list(FTP::Client* client, int socket_data)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				int32_t* fd = (int32_t*) client->get_cvar("fd");

				ftpdirent dirent;
				uint64_t nread;

				if(FTP::IO::readdir(*fd, &dirent, &nread) != 0)
				{
					FTP::IO::closedir(*fd);
					*fd = -1;

					client->send_code(451, "Failed to read directory.");
					return true;
				}

				if(nread == 0)
				{
					FTP::IO::closedir(*fd);
					*fd = -1;

					client->send_code(226, "Transfer complete.");
					return true;
				}

				#ifdef PS3
				if(FTP::Utilities::get_working_directory(*cwd_vector) == "/")
				{
					if(strcmp(dirent.d_name, "app_home") == 0
					|| strcmp(dirent.d_name, "host_root") == 0
					|| strcmp(dirent.d_name, "dev_flash2") == 0
					|| strcmp(dirent.d_name, "dev_flash3") == 0)
					{
						return false;
					}
				}
				#endif

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), dirent.d_name);
				char mode[11];

				ftpstat stat;
				if(FTP::IO::stat(path, &stat) == 0 && FTP::Utilities::get_file_mode(mode, stat))
				{
					char tstr[16];
					strftime(tstr, 15, "%b %e %H:%M", localtime(&stat.st_mtime));

					ssize_t len = sprintf(client->buffer_data,
						"%s %3" PRIuMAX " %-10d %-10d %10" PRIu64 " %s %s\r\n",
						mode, (uintmax_t) d_ino, stat.st_uid, stat.st_gid, (uint64_t) stat.st_size, tstr, dirent.d_name
					);

					ssize_t nwrite = send(socket_data, client->buffer_data, (size_t)len, 0);

					if(nwrite == -1 || nwrite < len)
					{
						FTP::IO::closedir(*fd);
						*fd = -1;

						client->send_code(451, "Data transfer error.");
						return true;
					}
				}

				return false;
			}

			bool nlst(FTP::Client* client, int socket_data)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				int32_t* fd = (int32_t*) client->get_cvar("fd");

				ftpdirent dirent;
				uint64_t nread;

				if(FTP::IO::readdir(*fd, &dirent, &nread) != 0)
				{
					FTP::IO::closedir(*fd);
					*fd = -1;

					client->send_code(451, "Failed to read directory.");
					return true;
				}

				if(nread == 0)
				{
					FTP::IO::closedir(*fd);
					*fd = -1;

					client->send_code(226, "Transfer complete.");
					return true;
				}

				#ifdef PS3
				if(FTP::Utilities::get_working_directory(*cwd_vector) == "/")
				{
					if(strcmp(dirent.d_name, "app_home") == 0
					|| strcmp(dirent.d_name, "host_root") == 0
					|| strcmp(dirent.d_name, "dev_flash2") == 0
					|| strcmp(dirent.d_name, "dev_flash3") == 0)
					{
						return false;
					}
				}
				#endif

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), dirent.d_name);

				ftpstat stat;
				if(FTP::IO::stat(path, &stat) == 0)
				{
					ssize_t len = sprintf(client->buffer_data, "%s\r\n", dirent.d_name);
					ssize_t nwrite = send(socket_data, client->buffer_data, (size_t)len, 0);

					if(nwrite == -1 || nwrite < len)
					{
						FTP::IO::closedir(*fd);
						*fd = -1;

						client->send_code(451, "Data transfer error.");
						return true;
					}
				}

				return false;
			}

			// retr
			bool sendfile(FTP::Client* client, int socket_data)
			{
				int32_t* fd = (int32_t*) client->get_cvar("fd");

				uint64_t read;

				if(FTP::IO::read(*fd, client->buffer_data, BUFFER_DATA, &read) != 0)
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(452, "Failed to read file");
					return true;
				}

				if(read == 0)
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(226, "Transfer complete.");
					return true;
				}

				ssize_t write = send(socket_data, client->buffer_data, (size_t) read, 0);

				if(write == -1 || (uint64_t) write < read)
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(451, "Error in data transmission");
					return true;
				}

				return false;
			}

			// stor, appe, stou
			bool recvfile(FTP::Client* client, int socket_data)
			{
				int32_t* fd = (int32_t*) client->get_cvar("fd");

				ssize_t read = recv(socket_data, client->buffer_data, BUFFER_DATA, 0);

				if(read == 0)
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(226, "Transfer complete.");
					return true;
				}

				if(read == -1)
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(451, "Error in data transmission.");
					return true;
				}

				uint64_t write;

				if(FTP::IO::write(*fd, client->buffer_data, read, &write) != 0
				|| write < (uint64_t) read)
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(452, "Failed to write data to file.");
					return true;
				}

				return false;
			}
		};

		namespace cmd
		{
			void abor(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				client->data_end();
				client->send_code(226, "Data transfer aborted.");
			}

			void acct(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(*auth)
				{
					client->send_code(202, "Already logged in");
					return;
				}

				*auth = true;
				client->send_code(230, "Logged in");
			}

			void allo(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				client->data_end();
				client->send_code(202, "OK");
			}

			void cdup(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				cwd_vector->pop_back();
				client->send_code(250, "Working directory changed.");
			}

			void cwd(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params.empty())
				{
					client->send_code(250, "Working directory unchanged.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				ftpstat stat;
				if(FTP::IO::stat(path, &stat) == 0 && (stat.st_mode & S_IFMT) == S_IFDIR)
				{
					FTP::Utilities::set_working_directory(cwd_vector, path);
					client->send_code(250, "Working directory changed.");
				}
				else
				{
					client->send_code(550, "Cannot access the specified directory.");
				}
			}

			void dele(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params.empty())
				{
					client->send_code(550, "No filename specified.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				if(FTP::IO::unlink(path) == 0)
				{
					client->send_code(250, "File deleted.");
				}
				else
				{
					client->send_code(450, "File cannot be deleted.");
				}
			}

			void help(FTP::Client* client, std::string cmd, std::string params)
			{
				client->send_code(214, WELCOME_MSG);
			}

			void list(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");
				int32_t* fd = (int32_t*) client->get_cvar("fd");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(*fd != -1)
				{
					client->send_code(450, "Another data transfer is already in progress.");
					return;
				}

				if(FTP::IO::opendir(FTP::Utilities::get_working_directory(*cwd_vector).c_str(), fd) != 0)
				{
					client->send_code(550, "Cannot access directory.");
					return;
				}

				if(client->data_start(feat::base::data::list, (POLLOUT|POLLWRNORM)))
				{
					struct linger optlinger;
					optlinger.l_onoff = 1;
					optlinger.l_linger = 0;
					setsockopt(client->get_data_socket(), SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

					client->send_code(150, "Accepted data connection");
				}
				else
				{
					FTP::IO::closedir(*fd);
					*fd = -1;

					client->send_code(425, "Cannot open data connection");
				}
			}

			void mkd(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params.empty())
				{
					client->send_code(550, "No filename specified.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				if(FTP::IO::mkdir(path, 0777) == 0)
				{
					client->send_code(257, "\"" + path + "\" was successfully created.");
				}
				else
				{
					client->send_code(550, "Cannot create directory.");
				}
			}

			void mode(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params == "s" || params == "S")
				{
					client->send_code(200, "OK");
				}
				else
				{
					client->send_code(504, "MODE not accepted.");
				}
			}

			void nlst(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");
				int32_t* fd = (int32_t*) client->get_cvar("fd");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(*fd != -1)
				{
					client->send_code(450, "Another data transfer is already in progress.");
					return;
				}

				if(FTP::IO::opendir(FTP::Utilities::get_working_directory(*cwd_vector).c_str(), fd) != 0)
				{
					client->send_code(550, "Cannot access directory.");
					return;
				}

				if(client->data_start(feat::base::data::nlst, (POLLOUT|POLLWRNORM)))
				{
					struct linger optlinger;
					optlinger.l_onoff = 1;
					optlinger.l_linger = 0;
					setsockopt(client->get_data_socket(), SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));

					client->send_code(150, "Accepted data connection");
				}
				else
				{
					FTP::IO::closedir(*fd);
					*fd = -1;

					client->send_code(425, "Cannot open data connection");
				}
			}

			void noop(FTP::Client* client, std::string cmd, std::string params)
			{
				client->send_code(200, "OK");
			}

			void pass(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");
				std::string* user = (std::string*) client->get_cvar("user");

				if(*auth)
				{
					client->send_code(202, "Already logged in");
					return;
				}

				if(client->last_cmd != "USER")
				{
					client->send_code(503, "Login with USER first");
					return;
				}

				*auth = true;
				client->send_code(230, "Successfully logged in as " + *user);
			}

			void pasv(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				struct sockaddr_in pasv_addr;

				if(client->pasv_enter(&pasv_addr))
				{
					char pasv_msg[64];

					sprintf(pasv_msg,
						"Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
						((htonl(pasv_addr.sin_addr.s_addr) & 0xff000000) >> 24),
						((htonl(pasv_addr.sin_addr.s_addr) & 0x00ff0000) >> 16),
						((htonl(pasv_addr.sin_addr.s_addr) & 0x0000ff00) >>  8),
						(htonl(pasv_addr.sin_addr.s_addr) & 0x000000ff),
						((htons(pasv_addr.sin_port) & 0xff00) >> 8),
						(htons(pasv_addr.sin_port) & 0x00ff));
					
					client->send_code(227, pasv_msg);
				}
				else
				{
					client->send_code(425, "Failed to enter passive mode.");
				}
			}

			void port(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				void* cvar_ptr = client->get_cvar("port_addr");

				if(cvar_ptr != NULL)
				{
					struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
					delete port_addr;
					client->set_cvar("port_addr", NULL);
				}

				unsigned short port_tuple[6];
				int count = FTP::Utilities::parse_port_tuple(port_tuple, params.c_str());

				if(count != 6)
				{
					client->send_code(501, "Bad syntax.");
					return;
				}

				if(client->get_data_socket() != -1)
				{
					client->send_code(450, "Data connection already active.");
					return;
				}

				struct sockaddr_in* port_addr = new (std::nothrow) struct sockaddr_in;
				client->set_cvar("port_addr", (void*) port_addr);

				port_addr->sin_family = AF_INET;
				port_addr->sin_port = htons(port_tuple[4] << 8 | port_tuple[5]);
				port_addr->sin_addr.s_addr = htonl(
					((unsigned char)(port_tuple[0]) << 24) +
					((unsigned char)(port_tuple[1]) << 16) +
					((unsigned char)(port_tuple[2]) << 8) +
					((unsigned char)(port_tuple[3]))
				);

				client->send_code(200, "PORT ready.");
			}

			void pwd(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				client->send_code(257, "\"" + FTP::Utilities::get_working_directory(*cwd_vector) + "\" is the current directory.");
			}

			void quit(FTP::Client* client, std::string cmd, std::string params)
			{
				client->send_code(221, "Bye.");

				shutdown(client->get_control_socket(), SHUT_RDWR);
			}

			void rest(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				uint64_t* rest = (uint64_t*) client->get_cvar("rest");

				int64_t rest_param = atoll(params.c_str());

				if(rest_param >= 0)
				{
					*rest = rest_param;

					std::stringstream rest_str;
					rest_str << rest_param;

					client->send_code(350, "Restarting at " + rest_str.str());
				}
				else
				{
					client->send_code(554, "Invalid restart point");
				}
			}

			void retr(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");
				int32_t* fd = (int32_t*) client->get_cvar("fd");
				uint64_t* rest = (uint64_t*) client->get_cvar("rest");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(*fd != -1)
				{
					client->send_code(450, "Another data transfer is already in progress.");
					return;
				}

				if(params.empty())
				{
					client->send_code(501, "No filename specified");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				if(!FTP::Utilities::file_exists(path))
				{
					client->send_code(550, "File does not exist");
					return;
				}

				if(FTP::IO::open(path, O_RDONLY, fd) != 0)
				{
					client->send_code(550, "Cannot open file");
					return;
				}

				uint64_t pos;
				FTP::IO::lseek(*fd, *rest, SEEK_SET, &pos);
				*rest = 0;

				if(client->data_start(feat::base::data::sendfile, POLLOUT|POLLWRNORM))
				{
					client->send_code(150, "Accepted data connection");
				}
				else
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(425, "Cannot open data connection");
				}
			}

			void rmd(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params.empty())
				{
					client->send_code(550, "No filename specified.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				if(FTP::IO::rmdir(path) == 0)
				{
					client->send_code(250, "Directory removed.");
				}
				else
				{
					client->send_code(450, "Directory cannot be removed.");
				}
			}

			void rnfr(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				std::string* rnfr = (std::string*) client->get_cvar("rnfr");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				ftpstat stat;
				if(FTP::IO::stat(path, &stat) == 0)
				{
					*rnfr = path;

					client->send_code(350, "RNFR ready.");
				}
				else
				{
					client->send_code(550, "File or directory not found.");
				}
			}

			void rnto(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				std::string* rnfr = (std::string*) client->get_cvar("rnfr");
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(client->last_cmd != "RNFR" || rnfr->empty())
				{
					client->send_code(503, "RNFR not set.");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				if(FTP::IO::rename(*rnfr, path) == 0)
				{
					client->send_code(250, "Successfully renamed file or directory.");
				}
				else
				{
					client->send_code(550, "Could not rename file or directory.");
				}
			}

			void site(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params.empty())
				{
					client->send_code(501, "No SITE command specified.");
					return;
				}

				FTP::Command site_command = site::get_commands();
				
				std::pair<std::string, std::string> site_cmd;
				site_cmd = FTP::Utilities::parse_command_string(params.c_str());

				if(!site_command.call_command(site_cmd, client))
				{
					client->send_code(500, "SITE command not implemented.");
				}
			}

			void stat(FTP::Client* client, std::string cmd, std::string params)
			{
				std::string* user = (std::string*) client->get_cvar("user");
				bool* auth = (bool*) client->get_cvar("auth");

				std::stringstream connections_ss;
				connections_ss << client->server->get_num_connections();

				std::string is_auth(*auth ? "yes" : "no");

				client->send_multicode(211, WELCOME_MSG);
				client->send_multimessage("Username: " + *user);
				client->send_multimessage("Authenticated: " + is_auth);
				client->send_multimessage("Total connections: " + connections_ss.str());
				client->send_code(211, "End");
			}

			void stor(FTP::Client* client, std::string cmd, std::string params)
			{
				std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
				bool* auth = (bool*) client->get_cvar("auth");
				int32_t* fd = (int32_t*) client->get_cvar("fd");
				uint64_t* rest = (uint64_t*) client->get_cvar("rest");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(*fd != -1)
				{
					client->send_code(450, "Another data transfer is already in progress.");
					return;
				}

				if(cmd == "STOU")
				{
					int i = 0;
					do
					{
						if(i > 0 || params.empty())
						{
							std::stringstream u_ss;

							if(!params.empty())
							{
								u_ss << '.';
							}

							u_ss << i;

							params = params + u_ss.str();
						}
						
						i++;
					} while(FTP::Utilities::file_exists(FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params)));
				}

				if(params.empty())
				{
					client->send_code(501, "No filename specified");
					return;
				}

				std::string path = FTP::Utilities::get_absolute_path(FTP::Utilities::get_working_directory(*cwd_vector), params);

				uint32_t oflags = O_WRONLY;

				if(!FTP::Utilities::file_exists(path))
				{
					oflags |= O_CREAT;
				}

				if(cmd == "APPE")
				{
					oflags |= O_APPEND;
				}
				else
				{
					if(*rest == 0)
					{
						oflags |= O_TRUNC;
					}
				}

				if(FTP::IO::open(path, oflags, fd) != 0)
				{
					client->send_code(550, "Cannot access file");
					return;
				}

				if(oflags & O_CREAT)
				{
					FTP::IO::chmod(path, 0777);
				}

				uint64_t pos;
				FTP::IO::lseek(*fd, *rest, SEEK_SET, &pos);
				*rest = 0;

				if(client->data_start(feat::base::data::recvfile, POLLIN|POLLRDNORM))
				{
					struct linger optlinger;
					optlinger.l_onoff = 1;
					optlinger.l_linger = 0;
					setsockopt(client->get_data_socket(), SOL_SOCKET, SO_LINGER, &optlinger, sizeof(optlinger));
					
					client->send_code(150, "Accepted data connection.");
				}
				else
				{
					FTP::IO::close(*fd);
					*fd = -1;

					client->send_code(425, "Cannot open data connection.");
				}
			}

			void stru(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				if(params == "f" || params == "F")
				{
					client->send_code(200, "OK");
				}
				else
				{
					client->send_code(504, "STRU not accepted.");
				}
			}

			void syst(FTP::Client* client, std::string cmd, std::string params)
			{
				client->send_code(215, "UNIX Type: L8");
			}

			void type(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");

				if(!*auth)
				{
					client->send_code(530, "Not logged in");
					return;
				}

				client->send_code(200, "OK");
			}

			void user(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");
				std::string* user = (std::string*) client->get_cvar("user");

				if(*auth)
				{
					client->send_code(202, "Already logged in");
					return;
				}

				if(params.empty())
				{
					client->send_code(501, "No username specified");
					return;
				}

				*user = params;
				client->send_code(331, "Username " + params + " OK. Password required");
			}
		};

		void register_cvars(FTP::Client* client)
		{
			std::vector<std::string>* cwd_vector = new (std::nothrow) std::vector<std::string>;
			std::string* user = new (std::nothrow) std::string;
			std::string* rnfr = new (std::nothrow) std::string;
			bool* auth = new (std::nothrow) bool;
			int32_t* fd = new (std::nothrow) int32_t;
			uint64_t* rest = new (std::nothrow) uint64_t;

			*auth = false;
			*fd = -1;
			*rest = 0;

			client->set_cvar("cwd_vector", (void*) cwd_vector);
			client->set_cvar("user", (void*) user);
			client->set_cvar("rnfr", (void*) rnfr);
			client->set_cvar("auth", (void*) auth);
			client->set_cvar("fd", (void*) fd);
			client->set_cvar("rest", (void*) rest);
		}

		void unregister_cvars(FTP::Client* client)
		{
			std::vector<std::string>* cwd_vector = (std::vector<std::string>*) client->get_cvar("cwd_vector");
			std::string* user = (std::string*) client->get_cvar("user");
			std::string* rnfr = (std::string*) client->get_cvar("rnfr");
			bool* auth = (bool*) client->get_cvar("auth");
			int32_t* fd = (int32_t*) client->get_cvar("fd");
			uint64_t* rest = (uint64_t*) client->get_cvar("rest");

			void* cvar_ptr = client->get_cvar("port_addr");

			if(cvar_ptr != NULL)
			{
				struct sockaddr_in* port_addr = (struct sockaddr_in*) cvar_ptr;
				delete port_addr;
			}

			delete cwd_vector;
			delete user;
			delete rnfr;
			delete auth;
			delete fd;
			delete rest;
		}

		FTP::Command get_commands(void)
		{
			FTP::Command command;

			command.register_connect_callback(register_cvars);
			command.register_disconnect_callback(unregister_cvars);

			command.register_command("ABOR", feat::base::cmd::abor);
			command.register_command("ACCT", feat::base::cmd::acct);
			command.register_command("ALLO", feat::base::cmd::allo);
			command.register_command("APPE", feat::base::cmd::stor);
			command.register_command("CDUP", feat::base::cmd::cdup);
			command.register_command("CWD", feat::base::cmd::cwd);
			command.register_command("DELE", feat::base::cmd::dele);
			command.register_command("HELP", feat::base::cmd::help);
			command.register_command("LIST", feat::base::cmd::list);
			command.register_command("MKD", feat::base::cmd::mkd);
			command.register_command("MODE", feat::base::cmd::mode);
			command.register_command("NLST", feat::base::cmd::nlst);
			command.register_command("NOOP", feat::base::cmd::noop);
			command.register_command("PASS", feat::base::cmd::pass);
			command.register_command("PASV", feat::base::cmd::pasv);
			command.register_command("PORT", feat::base::cmd::port);
			command.register_command("PWD", feat::base::cmd::pwd);
			command.register_command("QUIT", feat::base::cmd::quit);
			command.register_command("REST", feat::base::cmd::rest);
			command.register_command("RETR", feat::base::cmd::retr);
			command.register_command("RMD", feat::base::cmd::rmd);
			command.register_command("RNFR", feat::base::cmd::rnfr);
			command.register_command("RNTO", feat::base::cmd::rnto);
			command.register_command("SITE", feat::base::cmd::site);
			command.register_command("STAT", feat::base::cmd::stat);
			command.register_command("STOR", feat::base::cmd::stor);
			command.register_command("STOU", feat::base::cmd::stor);
			command.register_command("STRU", feat::base::cmd::stru);
			command.register_command("SYST", feat::base::cmd::syst);
			command.register_command("TYPE", feat::base::cmd::type);
			command.register_command("USER", feat::base::cmd::user);

			command.register_command("XCUP", feat::base::cmd::cdup);
			command.register_command("XCWD", feat::base::cmd::cwd);
			command.register_command("XMKD", feat::base::cmd::mkd);
			command.register_command("XPWD", feat::base::cmd::pwd);
			command.register_command("XRMD", feat::base::cmd::rmd);

			return command;
		}
	};
};
