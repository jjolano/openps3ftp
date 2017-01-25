#ifdef __cplusplus
extern "C" {
#endif

struct app_status {
	int running;
};

#ifdef __cplusplus
}
#endif

void server_start(void* arg);
