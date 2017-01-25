struct app_status {
	int is_running;
};

// arg is pointer to app_status
// set is_running to 1 when creating app_status
void server_start(void* arg);
