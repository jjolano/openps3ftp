#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include <sys/mutex.h>
#include <sys/systime.h>
#include <sys/thread.h>

#include "ntfs.h"
#include "ps3ntfs.h"

sys_mutex_t mutex;

ntfs_md* mounts = NULL;
int num_mounts = 0;

ntfs_md* ps3ntfs_prx_mounts(void)
{
	return mounts;
}

int ps3ntfs_prx_num_mounts(void)
{
	return num_mounts;
}

void ps3ntfs_prx_lock(void)
{
	sysMutexLock(mutex, 0);
}

void ps3ntfs_prx_unlock(void)
{
	sysMutexUnlock(mutex);
}

void ps3ntfs_automount(void* ptr)
{
	bool* running = ptr;

	sys_mutex_attr_t attr = {
		SYS_MUTEX_PROTOCOL_FIFO,
		SYS_MUTEX_ATTR_NOT_RECURSIVE,
		SYS_MUTEX_ATTR_PSHARED,
		SYS_MUTEX_ATTR_ADAPTIVE,
		0, 0, 0,
		""
	};

	sysMutexCreate(&mutex, &attr);

	bool is_mounted[8];
	memset(is_mounted, false, sizeof(is_mounted));

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
	
	while(*running)
	{
		// Iterate ports and mount NTFS.
		int i;
		for(i = 0; i < 8; i++)
		{
			if(PS3_NTFS_IsInserted(i))
			{
				if(!is_mounted[i])
				{
					sec_t* partitions = NULL;
					int num_partitions = ntfsFindPartitions(ntfs_usb_if[i], &partitions);

					if(num_partitions > 0 && partitions)
					{
						ps3ntfs_prx_lock();

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

						ps3ntfs_prx_unlock();

						free(partitions);
					}

					is_mounted[i] = true;
				}
			}
			else
			{
				if(is_mounted[i])
				{
					ps3ntfs_prx_lock();

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

					ps3ntfs_prx_unlock();

					is_mounted[i] = false;
				}
			}
		}

		sysSleep(1);
	}

	// Unmount NTFS.
	ps3ntfs_prx_lock();

	while(num_mounts-- > 0)
	{
		ntfsUnmount(mounts[num_mounts].name, true);
	}

	if(mounts != NULL)
	{
		free(mounts);
		mounts = NULL;
	}

	ps3ntfs_prx_unlock();

	sysMutexDestroy(mutex);
	
	sysThreadExit(0);
}
