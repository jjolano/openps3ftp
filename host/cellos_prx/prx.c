#include "prx.h"

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_exit);
SYS_MODULE_INFO(FTPD, SYS_MODULE_ATTR_EXCLUSIVE_LOAD | SYS_MODULE_ATTR_EXCLUSIVE_START, 4, 4);

struct FTPCommand ftp_command;
struct FTPServer ftp_server;

sys_ppu_thread_t prx_tid;

volatile bool prx_running;

inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = sys_prx_get_my_module_id();

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uintptr_t) meminfo);
}

void prx_unload(void)
{
	sys_prx_id_t prx = sys_prx_get_my_module_id();
	system_call_3(483, prx, 0, NULL);
}

void ftp_stop(void)
{
	prx_running = false;

	ftpserv_stop(&ftp_server);

	uint64_t prx_exitcode;
	sys_ppu_thread_join(prx_tid, &prx_exitcode);
}

int prx_stop(void)
{
	ftp_stop();
	finalize_module();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

int prx_exit(void)
{
	ftp_stop();
	prx_unload();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

void ftp_main(uint64_t ptr)
{
	uint32_t ret = 0;

	// initialize command struct
	ftpcmd_create(&ftp_command);

	// import commands...
	// base
	register_cmd(&ftp_command, "ABOR", ssftpCmdAbor);
	register_cmd(&ftp_command, "ACCT", ssftpCmdAcct);
	register_cmd(&ftp_command, "ALLO", ssftpCmdAllo);
	register_cmd(&ftp_command, "APPE", ssftpCmdStor);
	register_cmd(&ftp_command, "CDUP", ssftpCmdCdup);
	register_cmd(&ftp_command, "CWD", ssftpCmdCwd);
	register_cmd(&ftp_command, "DELE", ssftpCmdDele);
	register_cmd(&ftp_command, "HELP", ssftpCmdHelp);
	register_cmd(&ftp_command, "LIST", ssftpCmdList);
	register_cmd(&ftp_command, "MKD", ssftpCmdMkd);
	register_cmd(&ftp_command, "MODE", ssftpCmdMode);
	register_cmd(&ftp_command, "NLST", ssftpCmdNlst);
	register_cmd(&ftp_command, "NOOP", ssftpCmdNoop);
	register_cmd(&ftp_command, "PASS", ssftpCmdPass);
	register_cmd(&ftp_command, "PASV", ssftpCmdPasv);
	register_cmd(&ftp_command, "PORT", ssftpCmdPort);
	register_cmd(&ftp_command, "PWD", ssftpCmdPwd);
	register_cmd(&ftp_command, "QUIT", ssftpCmdQuit);
	register_cmd(&ftp_command, "REST", ssftpCmdRest);
	register_cmd(&ftp_command, "RETR", ssftpCmdRetr);
	register_cmd(&ftp_command, "RMD", ssftpCmdRmd);
	register_cmd(&ftp_command, "RNFR", ssftpCmdRnfr);
	register_cmd(&ftp_command, "RNTO", ssftpCmdRnto);
	register_cmd(&ftp_command, "SITE", ssftpCmdSite);
	register_cmd(&ftp_command, "STAT", ssftpCmdStat);
	register_cmd(&ftp_command, "STOR", ssftpCmdStor);
	register_cmd(&ftp_command, "STRU", ssftpCmdStru);
	register_cmd(&ftp_command, "SYST", ssftpCmdSyst);
	register_cmd(&ftp_command, "TYPE", ssftpCmdType);
	register_cmd(&ftp_command, "USER", ssftpCmdUser);

	// ext
	register_cmd(&ftp_command, "SIZE", ssftpCmdSize);
	register_cmd(&ftp_command, "MDTM", ssftpCmdMdtm);

	// feat
	register_cmd(&ftp_command, "FEAT", ssftpCmdFeat);

	// site
	register_cmd2(&ftp_command, "CHMOD", ssftpCmdChmod);
	register_cmd2(&ftp_command, "STOP", ssftpCmdStop);

	while(prx_running)
	{
		sys_timer_sleep(5);

		ftpserv_create(&ftp_server, 21, &ftp_command);
		ret = ftpserv_run(&ftp_server);
		ftpserv_destroy(&ftp_server);
	}

	ftpcmd_destroy(&ftp_command);

	sys_ppu_thread_exit(ret);
}

void prx_main(uint64_t ptr)
{
	// initialize server
	sys_ppu_thread_t ftp_tid;
	sys_ppu_thread_create(&ftp_tid, ftp_main, 0, 1000, 0x4000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-FTPD");

	while(prx_running)
	{
		sys_timer_sleep(1);
	}

	prx_running = false;

	ftpserv_stop(&ftp_server);

	uint64_t ftp_exitcode;
	sys_ppu_thread_join(ftp_tid, &ftp_exitcode);
	
	sys_ppu_thread_exit(0);
}

int prx_start(size_t args, void* argv)
{
	prx_running = true;
	
	sys_ppu_thread_create(&prx_tid, prx_main, 0, 1001, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP");

	_sys_ppu_thread_exit(0);
	return SYS_PRX_START_OK;
}
