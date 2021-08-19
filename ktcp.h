#include <linux/fs.h>
#include <net/sock.h>

//typedef int (*ktcp_handler_cb_t)(void* buffer, size_t len);

struct handling_param_s{
	size_t (*callback)(void* buffer, size_t len, char* payload, dsm_channel_t *server_channel) ;
	//ktcp_handler_cb_t *callback;
	size_t initial_size;
	/* private */
	dsm_channel_t *server_channel;
};

//dsm_channel_t* ktcp_init(int server_id, struct super_block *sb, int port, char* ip, struct handling_param_s *hparam);
dsm_channel_t* ktcp_init(int server_id, struct super_block *sb, 
		int port, char ip[MAX_NODES][IP_MAX_SIZE], struct handling_param_s *hparam);
//int ktcp_send(int target_node_id, const char *buffer, size_t length, int port);
int ktcp_send(int target_node_id, const char *buffer, size_t length, dsm_channel_t* server_channel);
dsm_channel_t* ktcp_init_test(int server_id, struct super_block *sb, int port, char ip[MAX_NODES][IP_MAX_SIZE]);
void ktcp_destroy_test(dsm_channel_t *server_channel);

//dsm_channel_t* ktcp_init(int server_id, struct super_block *sb, int port, char* ip);
//int ktcp_init(int server_id, ktcp_handler_cb_t func);
//int ktcp_send(int target_node_id, const char *buffer, size_t length);
