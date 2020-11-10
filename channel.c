/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */

#include "channel.h"
#include "internal.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>

int test;

#define __BLOCKED_HASH_BITS	7
static DEFINE_HASHTABLE(dsm_comm_htable, __BLOCKED_HASH_BITS);
static DEFINE_SPINLOCK(dsm_comm_hlock);

struct dsm_comm_hentry
{
	int tgt_id;
	dsm_request_t request;
	struct hlist_node hlink;
};

void print_request(dsm_request_t *request);

#define MAX_SEMA 12+1
struct semaphore dsm_request_semaphores[MAX_SEMA][DSM_REQ_NUM];
struct semaphore dsm_response_semaphores[MAX_SEMA];
		
static void sema_up(int sema_id, int response, int req_type)
{
	dsm_debug("sema %d is it a response? %d, req_type %d\n", sema_id, response, req_type);

	if(response)
		up(&dsm_response_semaphores[sema_id]);
	else
		up(&dsm_request_semaphores[sema_id][req_type-1]);
}

static int sema_down(int sema_id, int response, int req_type)
{
	dsm_debug("sema %d is it a response? %d, req_type %d\n", sema_id, response, req_type);

	if(response)
		return down_interruptible(&dsm_response_semaphores[sema_id]);
	else
		return down_interruptible(&dsm_request_semaphores[sema_id][req_type-1]);
}

static int channel_put_request(int target_id, int local_id, dsm_request_t *request)
{
	int src_id;
 	struct dsm_comm_hentry *entry;

	src_id = request->src_id;

	entry = kmalloc(sizeof(struct dsm_comm_hentry), GFP_KERNEL);
	entry->tgt_id = target_id;
	entry->request = *request;
	if(request->length)
	{
		entry->request.payload = kmalloc(request->length, GFP_KERNEL);
		memcpy(entry->request.payload, request->payload, request->length);
	}

	print_request(request);
	print_request(&entry->request);
	
	if(request->length)
	{
		dsm_debug("hash %d %d\n", jhash(entry->request.payload,entry->request.length,0), jhash(entry->request.payload,entry->request.length,0));
		dsm_debug("content int0 %d\n", *((int*)(entry->request.payload)));
		dsm_debug("content int0 %d\n", *((int*)(request->payload)));
	}
	//dsm_debug("content int0 %d\n", *((int*)(request->payload)));
	spin_lock(&dsm_comm_hlock);
	hash_add(dsm_comm_htable, &entry->hlink, request->tx_id);
	spin_unlock(&dsm_comm_hlock);
	dsm_debug("");

	if(local_id == src_id)	//this is a request
		sema_up(target_id, 0, request->req_type);
	else if (target_id != src_id)	//this is a request (forwarded)
		sema_up(target_id, 0, request->req_type);
	else			//this is a response
		sema_up(target_id, 1, request->req_type);

	return 0;
}

static struct dsm_request_s* channel_get_request(int local_id, enum dsm_request_type req_type)
{
	int ret;
	int bkt;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;

	found = 0;
	entry = NULL;
	request = NULL;

	dsm_debug("DSMFS: %s: local_id %d sema %d\n", 
				__func__, local_id, -1);
	ret=sema_down(local_id, 0, req_type);
	if(ret<0)
		goto out_err;


	spin_lock(&dsm_comm_hlock);
	//hash_for_each_possible(dsm_comm_htable, entry, hlink, (long) sock) {
	hash_for_each(dsm_comm_htable, bkt, entry, hlink) {
		request = &entry->request;
		dsm_debug("DSMFS: %s: local_id %d tgt_id %d\n", 
				__func__, local_id, entry->tgt_id);
		if(entry->tgt_id==local_id && request->src_id != local_id && (req_type == request->req_type))
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

	if(found && request->length)
		dsm_debug("hash %d", jhash(request->payload, request->length, 0));

	if(!found)
	{
		sema_up(local_id, 0, req_type);//the request is for another thread
		msleep(1);//TODO: remove me?
	}

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

	dsm_debug("DSMFS: %s: local_id %d sema %d\n", 
				__func__, local_id, 0);
	ret=sema_down(local_id, 1, -1);
	if(ret<0)
		goto out_err;

	spin_lock(&dsm_comm_hlock);
	dsm_debug("DSMFS: %s:%d\n", __func__, __LINE__);
	hash_for_each_possible(dsm_comm_htable, entry, hlink, (long) tx_id) {
	//hash_for_each(dsm_comm_hlock, bkt, entry, hlink) {
		dsm_debug("");
		request = &entry->request;
		print_request(request);
		dsm_debug("");
		//if(request->src_id==local_id)
		if(entry->tgt_id==local_id && request->src_id == local_id)
		{
			dsm_debug("DSMFS: %s:%d\n", __func__, __LINE__);
			found=1;
			break;
		}
	}

	if(found)
		hash_del(&entry->hlink);
	else
		request=NULL;
	spin_unlock(&dsm_comm_hlock);

	if(found && request->length)
		dsm_debug("hash %d", jhash(request->payload, request->length, 0));

	if(!found)
	{
		sema_up(local_id, 1, -1);//the request maybe for another thread
		msleep(1);//TODO: remove me?
	}

out_err:
	return request;
}


static void dsm_channel_init(int local_id)
{
	int i,j;

	if(local_id != 0)/* only 0 initialize the channels */
		return;
		
	for (i=0; i< MAX_SEMA; i++)
	{
		for (j=0; j< DSM_REQ_NUM; j++)
			sema_init(&dsm_request_semaphores[i][j], 0);
		sema_init(&dsm_response_semaphores[i], 0);
	}
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

void dsm_drop_request(dsm_request_t* request)
{
	struct dsm_comm_hentry *entry=NULL;

	BUG_ON(!request);

	if(request->length)
		kfree(request->payload);
	entry = container_of(request, struct dsm_comm_hentry, request);
	kfree(entry);
}

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	dsm_request_t * ret=NULL;
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);

		if(tx_id==-1)
			ret=channel_get_request(server_channel->id, req_type);
		else
			ret=channel_get_response(server_channel->id, tx_id);
	//}while(ret==NULL && !kthread_should_stop()); 
	}while(ret==NULL); 

	*request=ret;

	return 0;
}

int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request)
{
	channel_put_request(target_node, server_channel->id, request);
	return 0;
}
