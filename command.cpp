#include <string>
#include <vector>
#include <map>

#include <sys/stat.h>
#include <sys/file.h>

#include "client.h"
#include "command.h"

using namespace std;

void cmd_noop(Client* client, string params)
{
    client->send_code(200, "NOOP ok.");
}

void cmd_clnt(Client* client, string params)
{
    client->send_code(200, params + " sucks!");
}

void cmd_acct(Client* client, string params)
{
    client->send_code(202, "Command ignored. Not implemented.");
}

void cmd_feat(Client* client, string params)
{
    vector<string> feat;

    feat.push_back("REST STREAM");
    feat.push_back("PASV");
    feat.push_back("MDTM");
    feat.push_back("SIZE");

    client->send_multicode(211, "Features:");

    for(vector<string>::iterator it = feat.begin(); it != feat.end(); it++)
    {
        client->send_string(" " + *it);
    }

    client->send_code(211, "End");
}

void cmd_syst(Client* client, string params)
{
    client->send_code(215, "UNIX Type: L8");
}

void cmd_user(Client* client, string params)
{
    if(client->cvar_auth)
    {
        // already logged in
        client->send_code(230, "Already logged in");
        return;
    }

    if(params.empty())
    {
        client->send_code(501, "No username specified");
        return;
    }

    // should accept any kind of username
    client->cvar_user = params;
    client->send_code(331, "Username " + params + " OK. Password required");
}

void cmd_pass(Client* client, string params)
{
    // actual auth done here
    if(client->cvar_auth)
    {
        // already logged in
        client->send_code(230, "Already logged in");
        return;
    }

    if(client->lastcmd != "USER")
    {
        // user must be used first
        client->send_code(503, "Login with USER first");
        return;
    }

    client->cvar_auth = true;
    client->send_code(230, "Successfully logged in as " + client->cvar_user);
}

void register_cmds(map<string, cmdfunc>* cmd_handlers)
{
    // No authorization required commands
	register_cmd(cmd_handlers, "NOOP", cmd_noop);
	register_cmd(cmd_handlers, "CLNT", cmd_clnt);
	register_cmd(cmd_handlers, "ACCT", cmd_acct);
	register_cmd(cmd_handlers, "FEAT", cmd_feat);
	register_cmd(cmd_handlers, "SYST", cmd_syst);
	register_cmd(cmd_handlers, "USER", cmd_user);
	register_cmd(cmd_handlers, "PASS", cmd_pass);
}
