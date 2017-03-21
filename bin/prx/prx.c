#include "prx.h"

SYS_MODULE_START(prx_start);
SYS_MODULE_STOP(prx_stop);
SYS_MODULE_EXIT(prx_exit);
SYS_MODULE_INFO(FTPD, SYS_MODULE_ATTR_EXCLUSIVE_LOAD | SYS_MODULE_ATTR_EXCLUSIVE_START, 4, 2);

struct Server* ftp_server;
struct Command* ftp_command;

sys_ppu_thread_t prx_tid;
bool prx_running = false;

#ifdef _NTFS_SUPPORT_
#ifndef _PS3NTFS_PRX_
int spinlock_id;
ntfs_md* mounts = NULL;
int num_mounts = 0;

void ntfs_main(uint64_t ptr)
{
	bool is_mounted[8];
	memset(is_mounted, false, sizeof(is_mounted));

	sys_spinlock_initialize(&spinlock_id);

	const DISC_INTERFACE* ntfs_usb_if[8] = {
		&__io_ntfs_usb000,
		&__io_ntfs_usb001,
		&__io_ntfs_usb002,
		&__io_ntfs_usb003,
		&__io_ntfs_usb004,
		&__io_ntfs_usb005,
		&__io_ntfs_usb006,
		&__io_ntfs_usb007
	};

	sys_timer_sleep(5);
	
	while(prx_running)
	{
		// Iterate ports and mount NTFS.
		int i;
		for(i = 0; i < 8; i++)
		{
			if(PS3_NTFS_IsInserted(i))
			{
				if(!is_mounted[i])
				{
					//char msg[256];

					sec_t* partitions = NULL;
					int num_partitions = ntfsFindPartitions(ntfs_usb_if[i], &partitions);

					if(num_partitions > 0 && partitions)
					{
						sys_spinlock_lock(&spinlock_id);

						int j;
						for(j = 0; j < num_partitions; j++)
						{
							char name[32];
							sprintf(name, "ntfs%d-usb%03d", j, i);

							if(ntfsMount(name, ntfs_usb_if[i], partitions[j], CACHE_DEFAULT_PAGE_COUNT, CACHE_DEFAULT_PAGE_SIZE, NTFS_FORCE))
							{
								// add to mount struct
								mounts = (ntfs_md*) realloc(mounts, ++num_mounts * sizeof(ntfs_md));

								ntfs_md* mount = &mounts[num_mounts - 1];

								strcpy(mount->name, name);
								mount->interface = ntfs_usb_if[i];
								mount->startSector = partitions[j];
							}
						}

						sys_spinlock_unlock(&spinlock_id);

						free(partitions);
					}

					//sprintf(msg, "Mounted /dev_usb%03d", i);
					//vshtask_notify(msg);

					is_mounted[i] = true;
				}
			}
			else
			{
				//char msg[256];

				if(is_mounted[i])
				{
					sys_spinlock_lock(&spinlock_id);

					int j;
					for(j = 0; j < num_mounts; j++)
					{
						// unmount all partitions of this device
						if(mounts[j].interface == ntfs_usb_if[i])
						{
							ntfsUnmount(mounts[j].name, true);

							// realloc
							memmove(&mounts[j], &mounts[j + 1], (num_mounts - j - 1) * sizeof(ntfs_md));
							mounts = (ntfs_md*) realloc(mounts, --num_mounts * sizeof(ntfs_md));

							--j;
						}
					}

					sys_spinlock_unlock(&spinlock_id);

					//sprintf(msg, "Unmounted /dev_usb%03d", i);
					//vshtask_notify(msg);

					is_mounted[i] = false;
				}
			}
		}

		sys_timer_sleep(1);
	}

	sys_timer_sleep(2);

	// Unmount NTFS.
	sys_spinlock_lock(&spinlock_id);

	while(num_mounts-- > 0)
	{
		ntfsUnmount(mounts[num_mounts].name, true);
	}

	if(mounts != NULL)
	{
		free(mounts);
	}

	sys_spinlock_unlock(&spinlock_id);
	
	sys_ppu_thread_exit(0);
}
#endif
#endif

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

int prx_stop(void)
{
	if(prx_running)
	{
		prx_running = false;
		server_stop(ftp_server);

		uint64_t prx_exitcode;
		sys_ppu_thread_join(prx_tid, &prx_exitcode);
	}
	
	finalize_module();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

int prx_exit(void)
{
	if(prx_running)
	{
		prx_running = false;
		server_stop(ftp_server);

		uint64_t prx_exitcode;
		sys_ppu_thread_join(prx_tid, &prx_exitcode);
	}

	prx_unload();
	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}

void prx_main(uint64_t ptr)
{
	prx_running = true;

	#ifdef _NTFS_SUPPORT_
	#ifndef _PS3NTFS_PRX_
	sys_ppu_thread_t ntfs_tid;
	if(sys_ppu_thread_create(&ntfs_tid, ntfs_main, 0, 1001, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP-NTFS") != 0)
	{
		num_mounts = ntfsMountAll(&mounts, NTFS_FORCE);
	}
	#endif
	#endif

	ftp_command = (struct Command*) malloc(sizeof(struct Command));

	// initialize command struct
	command_init(ftp_command);

	// import commands...
	feat_command_import(ftp_command);
	base_command_import(ftp_command);
	ext_command_import(ftp_command);

	// wait for a bit for other plugins...
	sys_timer_sleep(11);

	if(prx_running)
	{
		// show startup msg
		vshtask_notify("OpenPS3FTP " APP_VERSION);

		ftp_server = (struct Server*) malloc(sizeof(struct Server));

		// let ftp library take over thread
		while(prx_running)
		{
			sys_timer_sleep(1);
			
			// initialize server struct
			server_init(ftp_server, ftp_command, 21);
			server_run(ftp_server);
			server_free(ftp_server);
		}
		
		free(ftp_server);
	}

	// server stopped, free resources
	command_free(ftp_command);
	free(ftp_command);

	#ifdef _NTFS_SUPPORT_
	#ifndef _PS3NTFS_PRX_
	uint64_t ntfs_exitcode;
	sys_ppu_thread_join(ntfs_tid, &ntfs_exitcode);
	#endif
	#endif
	
	sys_ppu_thread_exit(0);
}

int prx_start(size_t args, void* argv)
{
	if(sys_ppu_thread_create(&prx_tid, prx_main, 0, 1000, 0x4000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*) "OpenPS3FTP") != 0)
	{
		finalize_module();
		_sys_ppu_thread_exit(SYS_PRX_NO_RESIDENT);
		return SYS_PRX_NO_RESIDENT;
	}

	_sys_ppu_thread_exit(SYS_PRX_START_OK);
	return SYS_PRX_START_OK;
}
