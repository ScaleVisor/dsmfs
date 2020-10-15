#include "channel.h"
#include "internal.h"
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>

int test;

#define __BLOCKED_HASH_BITS	7
static DEFINE_HASHTABLE(dsm_comm_htable, __BLOCKED_HASH_BITS);
static DEFINE_SPINLOCK(dsm_comm_hlock);

struct dsm_comm_hentry
{
	int tgt_id;
	dsm_request_t* request;
	struct hlist_node hlink;
};

#define MAX_SEMA 2+1
struct semaphore dsm_comm_semaphores[MAX_SEMA];
		
static void sema_up(int sema_id)
{
	if(sema_id == -1)
		sema_id=MAX_SEMA-1;
	up(&dsm_comm_semaphores[sema_id]);
}

static int sema_down(int sema_id)
{
	if(sema_id == -1)
		sema_id=MAX_SEMA-1;
	return down_killable(&dsm_comm_semaphores[sema_id]);
}

static int channel_put_request(int target_id, int local_id, dsm_request_t* request)
{
 	struct dsm_comm_hentry *entry;
	entry = kmalloc(sizeof(struct dsm_comm_hentry), GFP_KERNEL);
	entry->tgt_id = target_id;
	entry->request = request;

	spin_lock(&dsm_comm_hlock);
	hash_add(dsm_comm_htable, &entry->hlink, request->tx_id);
	spin_unlock(&dsm_comm_hlock);

	if(local_id == request->src_id)	//this is a request
		sema_up(-1);
	else				//this is a response
		sema_up(0);

	return 0;
}

static struct dsm_request_s* channel_get_request(int local_id)
{
	int ret;
	int bkt;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;

	found = 0;
	entry = NULL;
	request = NULL;

	ret=sema_down(-1);
	if(ret<0)
		goto out_err;

	spin_lock(&dsm_comm_hlock);
	//hash_for_each_possible(dsm_comm_htable, entry, hlink, (long) sock) {
	hash_for_each(dsm_comm_htable, bkt, entry, hlink) {
		request = entry->request;
		if(entry->tgt_id!=local_id)
		{
			found=1;
			break;
		}
	}

	if(found)
		hash_del(&entry->hlink);
	else
		request=NULL;
	spin_unlock(&dsm_comm_hlock);

	if(!found)
		return NULL;

out_err:
	return request;
}

static struct dsm_request_s* channel_get_response(int local_id, int tx_id)
{
	int ret;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;

	found = 0;
	entry = NULL;
	request = NULL;

	ret=sema_down(0);
	if(ret<0)
		goto out_err;

	spin_lock(&dsm_comm_hlock);
	hash_for_each_possible(dsm_comm_htable, entry, hlink, (long) tx_id) {
	//hash_for_each(dsm_comm_hlock, bkt, entry, hlink) {
		request = entry->request;
		if(entry->tgt_id==local_id)
		{
			found=1;
			break;
		}
	}

	if(found)
		hash_del(&entry->hlink);
	else
		request=NULL;
	spin_unlock(&dsm_comm_hlock);

	if(!found)
		return NULL;

out_err:
	return request;
}


static void dsm_channel_init(int local_id)
{
	int i;

	if(local_id != 0)/* only 0 initialize the channels */
		return;
		
	for (i=0; i< MAX_SEMA; i++)
		sema_init(&dsm_comm_semaphores[i], 0);
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

	dsm_channel_init(local_id);

	return server_channel;
}

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id)
{
	if(tx_id==-1)
		*request=channel_get_request(server_channel->id);
	else
		*request=channel_get_response(server_channel->id, tx_id);
	return 0;
}

int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request, void* payload)
{
	request->payload=payload;
	channel_put_request(target_node, server_channel->id, request);
	return 0;
}
