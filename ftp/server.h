struct app_status {
	int is_running;
	int num_clients;
	int num_connections;
};

/* arg is pointer to app_status */
/* set is_running to 1 when creating app_status */
void server_start(void* arg);

/* for cellsdk */
void server_start_ex(uint64_t arg);
