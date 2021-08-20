#include "channel.h"
#include "internal.h"
#include "ktcp.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>

#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kvm_host.h>

#define tcp_printk(...) /**/

#if 0
static void* channel_handle_requests(void* arg)
{
	int ret = 0, idx;
        bool retry = false;
        char comm[TASK_COMM_LEN];
        dsm_request_t req;
        struct socket* accept_sock = (struct socket *)data;

        while (1) {
                if (kthread_should_stop()) {
                        ret = -EPIPE;
                        goto out;
                }

                len = ktcp_receive(accept_sock, &req);
                BUG_ON(len > 0 && len != sizeof(struct dsm_request));

                if (len <= 0) {
                        ret = len;
                        goto out;
                }
		/* place in hash table */
		htable_put_request(server_channel->htable, target_node, server_channel->id, request);
        }
out:
        get_task_comm(comm, current);
        dsm_debug("kvm[%d] %s exited server loop, error %d\n",
                                server_id, comm, ret);

        while (!kthread_should_stop()) {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule();
        }
	return NULL;
}
#endif

static size_t __tcp_callback(void* buffer, size_t len, char* payload, dsm_channel_t* server_channel)
{
	dsm_request_t * req=NULL;

	tcp_printk(KERN_INFO "%s:%d id %d received buffer: %ld\n", __func__, current->pid, server_channel->id, len);

	req=(dsm_request_t*)buffer;

	if(!payload && req->length)
	{
		//ask to be called with payload!
		tcp_printk(KERN_INFO "%s: received buffer: %ld\n", __func__, len);
		return req->length;
	}

	if(payload)
	{
		//BUG_ON(len!=PAGE_SIZE);
		//this must be a reponse: req+page!
		req->payload = payload;
	}

	tcp_printk(KERN_INFO "%s: sender_id %dsrc_id %d payload size: %d\n", __func__, req->src_id, req->sender_id, req->length);

	/*FIXME!!!*/
	htable_put_request(server_channel->htable, server_channel->id /*target node*/, req->sender_id /*sender ?*/, req);


	return 0;//next_size == 0
}

dsm_channel_t* dsm_channel_create(int server_id, struct super_block *sb, int port, char ip[MAX_NODES][IP_MAX_SIZE])
{

	/*
	dsm_channel_t *server_channel = kmalloc(sizeof(dsm_channel_t), GFP_KERNEL);
	server_channel->id=server_id;
	server_channel->sb=sb;
	server_channel->port=port;
	server_channel->ip=ip;

	server_channel->server_thread = ktcp_create_server(server_channel)

	return server_channel;
	*/
	dsm_channel_t* server_channel;
	struct handling_param_s *params;

	tcp_printk(KERN_INFO "%s started ip %s port %d %p\n", __func__, ip, port, __tcp_callback);

	params = kzalloc(sizeof(*params), GFP_KERNEL);//TODO: embed in ... or free
	params->callback = __tcp_callback;
	params->initial_size = sizeof(dsm_request_t);

	server_channel = ktcp_init(server_id, sb, port, ip, params);

	server_channel->htable = kzalloc(sizeof(struct channel_htable_s), GFP_KERNEL);

	htable_init(server_channel->htable);

	return server_channel;
}

//int dsm_channel_destroy(int server_id, struct super_block *sb)
void dsm_channel_destroy(dsm_channel_t* channel)
{
	//ktcp_destroy(...) TODO
	return;
}


int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request)
{
	int ret;
	//mm_segment_t oldmm;
	size_t buffer_size;
	char *local_buffer;

	request->sender_id = server_channel->id;

	buffer_size = sizeof(*request)+request->length;
	local_buffer = kzalloc(buffer_size, GFP_KERNEL);
	if (!local_buffer) {
		return -ENOMEM;
	}

	memcpy(local_buffer, request, sizeof(*request));
	memcpy(local_buffer + sizeof(*request), request->payload, request->length);

	tcp_printk(KERN_INFO "%s: this %d target_id %d payload size: %d\n", __func__, server_channel->id, target_node, request->length);

	ret = ktcp_send(target_node, local_buffer, buffer_size, server_channel);

	//int ktcp_send(int target_node_id, const char *buffer, size_t length, dsm_channel_t* server_channel)

	return ret < 0 ? ret : buffer_size;

}

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	/* should be called by dsm servers only? */
	int ret = 0;
	dsm_request_t * req=NULL;
	BUG_ON(tx_id!=-1);
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);
		ret=htable_get_request(server_channel->htable, server_channel->id, req_type, &req);
		dsm_debug("server_id %d tx_id %d got request\n", server_channel->id, tx_id);
	}while(!ret && req==NULL); 

	*request=req;
	return ret;

}

int dsm_channel_get_response(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	/* req_type ignored for now */
	int ret = 0;
	dsm_request_t * req=NULL;
	BUG_ON(tx_id==-1);
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);
		ret = htable_get_response(server_channel->htable, server_channel->id, tx_id, &req);
	}while(req==NULL); /* main difference with get_request : merge ? */

	*request=req;

	return ret;
}

void dsm_drop_request(dsm_request_t* request)
{

	BUG_ON(!request);
	if(request->length)
		kfree(request->payload);
	htable_drop_request(request);
}
