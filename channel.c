#include "channel.h"
#include <linux/slab.h>

dsm_channel_t* dsm_channel_create(int server_id,
					struct super_block *sb,
					int central_port,
					char* central_ip)
{
	dsm_channel_t *server_channel = kmalloc(sizeof(dsm_channel_t), GFP_KERNEL);
	server_channel->id=server_id;
	server_channel->sb=sb;
	//TODO: central_port
	//TODO: central_ip
	return server_channel;
}

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t* request)
{
	return -1;
}

int dsm_channel_send_request(int target_node, dsm_request_t* request, void* payload)
{
	return -1;
}
