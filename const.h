#include <sysutil/msg.h>

#define APP_VERSION     "4.0"

#define MOUNT_POINT     "/dev_blind"

#define LISTEN_BACKLOG  10
#define CMD_BUFFER      4096
#define DATA_BUFFER     64 * 1024

#define MSG_OK          (msgType)(MSG_DIALOG_NORMAL|MSG_DIALOG_BTN_TYPE_OK|MSG_DIALOG_DISABLE_CANCEL_ON)
