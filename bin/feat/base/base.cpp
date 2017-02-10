/* base.cpp: Base FTP commands. */

#include "common.hpp"
#include "server.hpp"
#include "client.hpp"
#include "command.hpp"

#include "feat.hpp"

namespace feat
{
	namespace base
	{
		namespace data
		{
			
		};

		namespace cmd
		{
			void abor(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void acct(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void allo(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void appe(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void cdup(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void cwd(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void dele(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void help(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void list(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void mkd(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void mode(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void nlst(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void noop(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void pass(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");
				std::string* user = (std::string*) client->get_cvar("user");

				if(*auth)
				{
					client->send_code(230, "Already logged in");
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

			}

			void port(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void pwd(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void quit(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void rein(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void rest(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void retr(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void rmd(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void rnfr(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void rnto(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void site(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void smnt(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void stat(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void stor(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void stou(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void stru(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void syst(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void type(FTP::Client* client, std::string cmd, std::string params)
			{

			}

			void user(FTP::Client* client, std::string cmd, std::string params)
			{
				bool* auth = (bool*) client->get_cvar("auth");
				std::string* user = (std::string*) client->get_cvar("user");

				if(*auth)
				{
					client->send_code(230, "Already logged in");
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
			std::vector<std::string>* cwd_vector = new std::vector<std::string>;
			std::string* user = new std::string;
			std::string* rnfr = new std::string;
			bool* auth = new bool;
			int32_t* fd = new int32_t;
			uint64_t* rest = new uint64_t;

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

			void* cvar_ptr = get_cvar("port_addr");

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
			command.register_command("APPE", feat::base::cmd::appe);
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
			command.register_command("REIN", feat::base::cmd::rein);
			command.register_command("REST", feat::base::cmd::rest);
			command.register_command("RETR", feat::base::cmd::retr);
			command.register_command("RMD", feat::base::cmd::rmd);
			command.register_command("RNFR", feat::base::cmd::rnfr);
			command.register_command("RNTO", feat::base::cmd::rnto);
			command.register_command("SITE", feat::base::cmd::site);
			command.register_command("SMNT", feat::base::cmd::smnt);
			command.register_command("STAT", feat::base::cmd::stat);
			command.register_command("STOR", feat::base::cmd::stor);
			command.register_command("STOU", feat::base::cmd::stou);
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
