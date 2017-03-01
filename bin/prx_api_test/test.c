#include "test.h"

bool is_module(const char* vsh_module)
{
	// 0x10000 = ELF
	// 0x10080 = segment 2 start
	// 0x10200 = code start

	uint32_t table = (*(uint32_t*)0x1008C) + 0x984; // vsh table address

	while(((uint32_t)*(uint32_t*)table) != 0)
	{
		uint32_t* export_stru_ptr = (uint32_t*)*(uint32_t*)table; // ptr to export stub, size 2C, "sys_io" usually... Exports:0000000000635BC0 stru_635BC0:    ExportStub_s <0x1C00, 1, 9, 0x39, 0, 0x2000000, aSys_io, ExportFNIDTable_sys_io, ExportStubTable_sys_io>

		const char* lib_name_ptr =  (const char*)*(uint32_t*)((char*)export_stru_ptr + 0x10);

		if(strncmp(vsh_module, lib_name_ptr, strlen(lib_name_ptr)) == 0)
		{
			return true;
		}

		table += 4;
	}

	return false;
}

void cmd_test(struct Client* client, const char command_name[32], const char* command_params)
{
	char buf[64];
	sys_prx_id_t module_id = sys_prx_get_module_id_by_name(command_params, 0, NULL);
	bool is_vsh_module = is_module(command_params);

	client_send_multicode(client, 200, "Command injection successful!");

	client_send_multimessage(client, "VSH Export Modules:");

	uint32_t table = (*(uint32_t*)0x1008C) + 0x984; // vsh table address

	while(((uint32_t)*(uint32_t*)table) != 0)
	{
		uint32_t* export_stru_ptr = (uint32_t*)*(uint32_t*)table; // ptr to export stub, size 2C, "sys_io" usually... Exports:0000000000635BC0 stru_635BC0:    ExportStub_s <0x1C00, 1, 9, 0x39, 0, 0x2000000, aSys_io, ExportFNIDTable_sys_io, ExportStubTable_sys_io>

		const char* lib_name_ptr =  (const char*)*(uint32_t*)((char*)export_stru_ptr + 0x10);

		client_send_multimessage(client, lib_name_ptr);

		table += 4;
	}

	sprintf(buf, "sys_prx_get_module_id_by_name: %d", module_id);
	client_send_multimessage(client, buf);

	sprintf(buf, "is_vsh_module: %d", is_vsh_module);
	client_send_multimessage(client, buf);

	client_send_code(client, 200, "Done.");
}
