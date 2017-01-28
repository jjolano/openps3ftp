#define APP_VERSION     "4.0"
#define WELCOME_MSG		"Welcome to OpenPS3FTP v" APP_VERSION "!"

#define LISTEN_BACKLOG  128
#define CMD_BUFFER      1024
#define DATA_BUFFER     128 * 1024

// experimental async writing support
// if compiling without sysfs, should be set to false
#define AIO_ENABLED		false

#define AIO_ACTIVE      1
#define AIO_WAITING		2
#define AIO_READY		0
