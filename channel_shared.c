#include "channel.h"
#include "internal.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>



int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request)
{
	htable_put_request(target_node, server_channel->id, request);
	return 0;
}

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	/* should be called by dsm servers only */
	int ret = 0;
	dsm_request_t * req=NULL;
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);
		BUG_ON(tx_id!=-1);
		ret=htable_get_request(server_channel->id, req_type, &req);
	}while(!ret && req==NULL); 

	*request=req;
	return ret;
}

int dsm_channel_get_response(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	/* req_type ignored for now */
	int ret = 0;
	dsm_request_t * req=NULL;
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);
		BUG_ON(tx_id==-1);
		ret = htable_get_response(server_channel->id, tx_id, &req);
	}while(req==NULL); /* main difference with get_request : merge ? */

	*request=req;

	return ret;
}

void dsm_drop_request(dsm_request_t* request)
{
	htable_drop_request(request);
}

dsm_channel_t* dsm_channel_create(int local_id,
					struct super_block *sb,
					int central_port,
					char* central_ip)
{
	dsm_channel_t *server_channel = kmalloc(sizeof(dsm_channel_t), GFP_KERNEL);
	server_channel->id=local_id;
	server_channel->sb=sb;
	//TODO: central_port
	//TODO: central_ip

	htable_init(local_id);

	return server_channel;
}
