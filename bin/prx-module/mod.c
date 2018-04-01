#include "prx.h"
#include "mod.h"

void if_init(int view)
{
	void* if_act0[3] = { prxmb_if_action, 0, 0 };
	plugin_setInterface(view, ACT0, if_act0);
}

int if_start(void* view)
{
	return SYS_PRX_START_OK;
}

int if_stop(void)
{
	return SYS_PRX_STOP_OK;
}

void if_exit(void)
{
	
}
