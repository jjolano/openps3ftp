#define __CELL_ASSERT__

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timer.h>
#include <sys/return_code.h>
#include <stddef.h>
#include <math.h>
#include <cell.h>
#include <netex/net.h>
#include <netex/libnetctl.h>
#include <sysutil/sysutil_sysparam.h>

#include "gcmutil.h"

#include "const.h"
#include "server.h"

#define COLOR_BUFFER_NUM	2
#define HOST_SIZE			(1*1024*1024)
#define CB_SIZE				(0x10000)

using namespace cell::Gcm;

extern "C" int32_t userMain(void);

uint32_t display_width;
uint32_t display_height; 
float display_aspect_ratio;
uint32_t color_pitch;
uint32_t depth_pitch;
uint32_t color_offset[COLOR_BUFFER_NUM];
uint32_t depth_offset;
uint32_t color_depth=4; /* ARGB */
uint32_t z_depth=4;     /* COMPONENT24 */

static uint32_t frame_index = 0;

static int32_t initDisplay(void)
{
	CellVideoOutResolution resolution;

	/* read the current video status
	   INITIAL DISPLAY MODE HAS TO BE SET BY RUNNING SETMONITOR.SELF */
	CellVideoOutState video_state;
	CELL_GCMUTIL_CHECK_ASSERT(cellVideoOutGetState(CELL_VIDEO_OUT_PRIMARY, 0, &video_state));

	switch (video_state.displayMode.aspect){
	case CELL_VIDEO_OUT_ASPECT_4_3:
		display_aspect_ratio=4.0f/3.0f;
		break;
	case CELL_VIDEO_OUT_ASPECT_16_9:
		display_aspect_ratio=16.0f/9.0f;
		break;
	default:
		printf("unknown aspect ratio %x\n", video_state.displayMode.aspect);
		display_aspect_ratio=16.0f/9.0f;
	}

	CELL_GCMUTIL_CHECK_ASSERT(cellVideoOutGetResolution(video_state.displayMode.resolutionId, &resolution));

	display_width  = resolution.width;
	display_height = resolution.height;
  
	CellVideoOutConfiguration videoCfg;
	memset(&videoCfg, 0, sizeof(CellVideoOutConfiguration));
	videoCfg.resolutionId = video_state.displayMode.resolutionId;
	videoCfg.format = CELL_VIDEO_OUT_BUFFER_COLOR_FORMAT_X8R8G8B8;
	videoCfg.pitch = cellGcmGetTiledPitchSize(cellGcmAlign(CELL_GCM_ZCULL_ALIGN_WIDTH, display_width)*color_depth);
	CELL_GCMUTIL_CHECK_ASSERT(cellVideoOutConfigure(CELL_VIDEO_OUT_PRIMARY, &videoCfg, NULL, 0));
  
	cellGcmSetFlipMode(CELL_GCM_DISPLAY_HSYNC);

	return 0;
}

static int32_t initBuffer(void)
{
  	/* get config */
	CellGcmConfig config;
	cellGcmGetConfiguration(&config);

	/* initialize local memory */
	cellGcmUtilInitializeLocalMemory((size_t)config.localAddress,
					 (size_t)config.localSize);

	/* head address of color and depth buffers */
	void *color_addr[COLOR_BUFFER_NUM];
	void *depth_addr;

	/* calculate pitch size of tiled region */
	color_pitch = cellGcmGetTiledPitchSize(display_width * color_depth);
	depth_pitch = cellGcmGetTiledPitchSize(display_width * z_depth);

	/* buffer's height and width must be multiple of 64kB to bind
	   with zcull memory */
	uint32_t zcull_height = cellGcmAlign(CELL_GCM_ZCULL_ALIGN_WIDTH, display_height);
	uint32_t zcull_width = cellGcmAlign(CELL_GCM_ZCULL_ALIGN_HEIGHT, display_width); 

	/* calculate color and depth buffer size of 1 frame */
	uint32_t color_buffer_size = color_pitch * zcull_height;
	uint32_t depth_buffer_size = depth_pitch * zcull_height;

	/* head address of tiled regions has to be placed on 64KB boundary */
	for(uint32_t i=0;i<COLOR_BUFFER_NUM;i++){
		color_addr[i] = cellGcmUtilAllocateLocalMemory(color_buffer_size, CELL_GCM_TILE_ALIGN_OFFSET);

		CELL_GCMUTIL_CHECK_ASSERT(cellGcmAddressToOffset(color_addr[i], &color_offset[i]));
	}

	depth_addr = cellGcmUtilAllocateLocalMemory(depth_buffer_size, CELL_GCM_TILE_ALIGN_OFFSET);
	CELL_GCMUTIL_CHECK_ASSERT(cellGcmAddressToOffset(depth_addr, &depth_offset));

	/* set a tile region */
	for (uint32_t i = 0; i < COLOR_BUFFER_NUM; i++) {
		CELL_GCMUTIL_CHECK(cellGcmSetTileInfo(i, CELL_GCM_LOCATION_LOCAL, 
					 color_offset[i],
					 color_buffer_size, color_pitch,
					 CELL_GCM_COMPMODE_DISABLED, 0, 0));

		/* bind a tiled region with RSX(R) */
		CELL_GCMUTIL_CHECK(cellGcmBindTile(i));
	}

	CELL_GCMUTIL_CHECK(cellGcmSetTileInfo(COLOR_BUFFER_NUM, CELL_GCM_LOCATION_LOCAL,
				 depth_offset, depth_buffer_size, depth_pitch,
				 CELL_GCM_COMPMODE_Z32_SEPSTENCIL, 0, 2));

	CELL_GCMUTIL_CHECK_ASSERT(cellGcmBindTile(COLOR_BUFFER_NUM));

	/* regist surface : set a color buffer a display output buffer */
	for (uint32_t i = 0; i < COLOR_BUFFER_NUM; i++) {
		CELL_GCMUTIL_CHECK_ASSERT(cellGcmSetDisplayBuffer(i, color_offset[i], color_pitch,
				display_width, display_height));
	}

	/* regist zcull : bind the zcull memory with a depth buffer */
	CELL_GCMUTIL_CHECK(cellGcmBindZcull(0, depth_offset, zcull_width, zcull_height, 0,
			CELL_GCM_ZCULL_Z24S8, CELL_GCM_SURFACE_CENTER_1,
			CELL_GCM_ZCULL_LESS, CELL_GCM_ZCULL_LONES,
			CELL_GCM_SCULL_SFUNC_LESS, 0x80, 0xff));

	return 0;
}

static void setDrawEnv(void)
{
	cellGcmSetColorMask(CELL_GCM_COLOR_MASK_B|
		CELL_GCM_COLOR_MASK_G|
		CELL_GCM_COLOR_MASK_R|
		CELL_GCM_COLOR_MASK_A);

	cellGcmSetColorMaskMrt(0);
	uint16_t x,y,w,h;
	float min, max;
	float scale[4],offset[4];

	x = 0;
	y = 0;
	w = display_width;
	h = display_height;
	min = 0.0f;
	max = 1.0f;
	scale[0] = w * 0.5f;
	scale[1] = h * -0.5f;
	scale[2] = (max - min) * 0.5f;
	scale[3] = 0.0f;
	offset[0] = x + scale[0];
	offset[1] = y + h * 0.5f;
	offset[2] = (max + min) * 0.5f;
	offset[3] = 0.0f;

	cellGcmSetViewport(x, y, w, h, min, max, scale, offset);
	cellGcmSetClearColor(0xFF000000);

	cellGcmSetDepthTestEnable(CELL_GCM_TRUE);
	cellGcmSetDepthFunc(CELL_GCM_LESS);

	/* clear frame buffer */
	cellGcmSetClearSurface(CELL_GCM_CLEAR_Z|
		CELL_GCM_CLEAR_R|
		CELL_GCM_CLEAR_G|
		CELL_GCM_CLEAR_B|
		CELL_GCM_CLEAR_A);
}

static void setRenderTarget(const uint32_t Index)
{
	CellGcmSurface sf;
	sf.colorFormat 	= CELL_GCM_SURFACE_A8R8G8B8;
	sf.colorTarget	= CELL_GCM_SURFACE_TARGET_0;
	sf.colorLocation[0]	= CELL_GCM_LOCATION_LOCAL;
	sf.colorOffset[0] 	= color_offset[Index];

	sf.colorPitch[0] 	= color_pitch;

	sf.colorLocation[1]	= CELL_GCM_LOCATION_LOCAL;
	sf.colorLocation[2]	= CELL_GCM_LOCATION_LOCAL;
	sf.colorLocation[3]	= CELL_GCM_LOCATION_LOCAL;
	sf.colorOffset[1] 	= 0;
	sf.colorOffset[2] 	= 0;
	sf.colorOffset[3] 	= 0;
	sf.colorPitch[1]	= 64;
	sf.colorPitch[2]	= 64;
	sf.colorPitch[3]	= 64;

	sf.depthFormat 	= CELL_GCM_SURFACE_Z24S8;
	sf.depthLocation	= CELL_GCM_LOCATION_LOCAL;
	sf.depthOffset	= depth_offset;
	sf.depthPitch 	= depth_pitch;

	sf.type		= CELL_GCM_SURFACE_PITCH;
	sf.antialias	= CELL_GCM_SURFACE_CENTER_1;

	sf.width 		= display_width;
	sf.height 		= display_height;
	sf.x 		= 0;
	sf.y 		= 0;
	cellGcmSetSurface(&sf);
}

static void waitFlip(void)
{
	while (cellGcmGetFlipStatus()!=0){
		sys_timer_usleep(300);
	}
}

static void flip(void)
{
	waitFlip();

	cellGcmResetFlipStatus();
	
	if(cellGcmSetFlip(frame_index) != CELL_OK) return;
	cellGcmFlush();

	cellGcmSetWaitFlip();

	/* New render target */
	frame_index = (frame_index+1)%COLOR_BUFFER_NUM;
}

static void initDbgFont(void)
{
	int frag_size = CELL_DBGFONT_FRAGMENT_SIZE; 
	int font_tex  = CELL_DBGFONT_TEXTURE_SIZE;
	int vertex_size = 1024 * CELL_DBGFONT_VERTEX_SIZE; 
	int local_size = frag_size + font_tex + vertex_size;
	void *localmem = cellGcmUtilAllocateLocalMemory(local_size, 128);

	CellDbgFontConfigGcm cfg;
	memset(&cfg, 0, sizeof(CellDbgFontConfigGcm));
	cfg.localBufAddr = (sys_addr_t)localmem; 
	cfg.localBufSize = local_size;
	cfg.mainBufAddr = NULL;
	cfg.mainBufSize = 0;
	cfg.option = CELL_DBGFONT_VERTEX_LOCAL |
				 CELL_DBGFONT_TEXTURE_LOCAL |
				 CELL_DBGFONT_SYNC_ON;

	cellDbgFontInitGcm(&cfg);
}

void sysutil_exit_callback(uint64_t status, uint64_t param, void* userdata)
{
	(void) param;

	if(status == CELL_SYSUTIL_REQUEST_EXITGAME)
	{
		app_status* appstatus = (app_status*)userdata;
		appstatus->is_running = 0;
	}
}

static void blackout(void)
{
	setRenderTarget(frame_index);/* specify a rendering surface */

	cellGcmSetClearColor(0xFF000000);/* use a black image for blackout */

	cellGcmSetClearSurface(CELL_GCM_CLEAR_Z|
		CELL_GCM_CLEAR_R|
		CELL_GCM_CLEAR_G|
		CELL_GCM_CLEAR_B|
		CELL_GCM_CLEAR_A);

	flip();
}

void load_sysmodules(void)
{
	cellSysmoduleInitialize();

	cellSysmoduleLoadModule(CELL_SYSMODULE_FS);
	cellSysmoduleLoadModule(CELL_SYSMODULE_SYSUTIL);
	cellSysmoduleLoadModule(CELL_SYSMODULE_NET);
	cellSysmoduleLoadModule(CELL_SYSMODULE_NETCTL);

	sys_net_initialize_network();
	cellNetCtlInit();
}

void unload_sysmodules(void)
{
	cellNetCtlTerm();
	sys_net_finalize_network();

	cellSysmoduleUnloadModule(CELL_SYSMODULE_FS);
	cellSysmoduleUnloadModule(CELL_SYSMODULE_SYSUTIL);
	cellSysmoduleUnloadModule(CELL_SYSMODULE_NET);
	cellSysmoduleUnloadModule(CELL_SYSMODULE_NETCTL);

	cellSysmoduleFinalize();
}

void prepareDraw(void)
{
	setRenderTarget(frame_index);
	setDrawEnv();
}

int userMain(void)
{
	int ret = 0;

	// Load sysmodules.
	load_sysmodules();

	// GCM stuff
	void* host_addr = memalign(1024*1024, HOST_SIZE);
	CELL_GCMUTIL_ASSERTS(host_addr != NULL,"memalign()");
	CELL_GCMUTIL_CHECK_ASSERT(cellGcmInit(CB_SIZE, HOST_SIZE, host_addr));

	if(initDisplay() != 0)
	{
		return -1;
	}

	if(initBuffer() != 0)
	{
		return -1;
	}

	initDbgFont();

	app_status status;
	status.is_running = 1;

	ret = cellSysutilRegisterCallback(0, sysutil_exit_callback, (void*)&status);  

	if(ret != CELL_OK)
	{
		return -1;
	}

	// Check network status.
	int netctl_state;
	CellNetCtlInfo netctl_info;

	cellNetCtlGetState(&netctl_state);

	if(netctl_state != CELL_NET_CTL_STATE_IPObtained)
	{
		// not connected
		cellDbgFontExitGcm();
		blackout();
		cellGcmSetWaitFlip();
		cellGcmUtilFinish();
		
		unload_sysmodules();
		return -1;
	}

	// Get IP address.
	cellNetCtlGetInfo(CELL_NET_CTL_INFO_IP_ADDRESS, &netctl_info);

	// Mount dev_blind.
	{
		system_call_8(837,
			(uint64_t)"CELL_FS_IOS:BUILTIN_FLSH1",
			(uint64_t)"CELL_FS_FAT",
			(uint64_t)"/dev_blind",
			0, 0, 0, 0, 0);
	}

	// Prepare Async IO.
	cellFsAioInit("/dev_hdd0");

	// Create server thread.
	sys_ppu_thread_t server_tid;
	sys_ppu_thread_create(&server_tid, server_start_ex, (uint64_t)&status, 1000, 0x10000, SYS_PPU_THREAD_CREATE_JOINABLE, (char*)"ftpd");

	// Application loop
	while(status.is_running)
	{
		ret = cellSysutilCheckCallback();

		if(ret != CELL_OK)
		{
			break;
		}

		prepareDraw();

		// draw text
		cellDbgFontPrintf(0.1f, 0.1f, 1.0f, 0xffffffff, "CellPS3FTP (OpenPS3FTP) " APP_VERSION);
		cellDbgFontPrintf(0.1f, 0.2f, 1.0f, 0xffffffff, "IP Address: %s", netctl_info.ip_address);
		cellDbgFontPrintf(0.1f, 0.3f, 1.0f, 0xffffffff, "Clients: %d", status.num_clients);
		cellDbgFontPrintf(0.1f, 0.4f, 1.0f, 0xffffffff, "Connections: %d", status.num_connections);
		
		cellDbgFontDrawGcm();

		flip();
	}

	status.is_running = 0;

	// Join server thread and wait for exit...
	uint64_t thread_exit;
	sys_ppu_thread_join(server_tid, &thread_exit);

	// Finish Async IO.
	cellFsAioFinish("/dev_hdd0");

	// Unmount dev_blind.
	{
		system_call_1(838, (uint64_t)"/dev_blind");
	}

	cellDbgFontExitGcm();
	blackout();
	cellGcmSetWaitFlip();
	cellGcmUtilFinish();
	
	unload_sysmodules();
	return 0;
}
