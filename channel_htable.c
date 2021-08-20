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

void print_request(dsm_request_t *request);

struct dsm_comm_hentry
{
	int tgt_id;
	dsm_request_t request;
	struct hlist_node hlink;
};

static void sema_up(channel_htable_t *htable, int sema_id, int response, int req_type)
{
	dsm_debug("sema %d is it a response? %d, req_type %d\n", sema_id, response, req_type);
	BUG_ON(sema_id>MAX_SEMA);

	if(response)
		up(&htable->dsm_response_semaphores[sema_id]);
	else
		up(&htable->dsm_request_semaphores[sema_id][req_type-1]);
}

static int sema_down(channel_htable_t *htable, int sema_id, int response, int req_type)
{
	dsm_debug("sema %d is it a response? %d, req_type %d\n", sema_id, response, req_type);

	if(response)
		return down_interruptible(&htable->dsm_response_semaphores[sema_id]);
	else
		return down_interruptible(&htable->dsm_request_semaphores[sema_id][req_type-1]);
}

int htable_put_request(channel_htable_t *htable, int target_id, int local_id, dsm_request_t *request)
{
	int src_id;
 	struct dsm_comm_hentry *entry;

	src_id = request->src_id;

	entry = kmalloc(sizeof(struct dsm_comm_hentry), GFP_KERNEL);/* free in htable_drop_request */
	entry->tgt_id = target_id;
	entry->request = *request;

	//dsm_debug("content int0 %d\n", *((int*)(request->payload)));
	spin_lock(&htable->htable_lock);
	hash_add(htable->main_htable, &entry->hlink, request->tx_id);
	spin_unlock(&htable->htable_lock);
	dsm_debug("");

	if(local_id == src_id)	//this is a request
		sema_up(htable, target_id, 0, request->req_type);
	else if (target_id != src_id)	//this is a request (forwarded)
		sema_up(htable, target_id, 0, request->req_type);
	else			//this is a response
		sema_up(htable, target_id, 1, request->req_type);

	return 0;
}

int htable_get_request(channel_htable_t *htable, int local_id, enum dsm_request_type req_type, dsm_request_t** ret_request)
{
	int ret;
	int bkt;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;

	ret = 0;
	found = 0;
	entry = NULL;
	request = NULL;

	dsm_debug("DSMFS: %s: local_id %d sema %d\n", 
				__func__, local_id, -1);
	ret=sema_down(htable, local_id, 0, req_type);
	if(ret<0)
		goto out_err;


	spin_lock(&htable->htable_lock);
	//hash_for_each_possible(htable->main_htable, entry, hlink, (long) sock) {
	hash_for_each(htable->main_htable, bkt, entry, hlink) {
		request = &entry->request;
		dsm_debug("DSMFS: %s: local_id %d tgt_id %d\n", 
				__func__, local_id, entry->tgt_id);
		if(entry->tgt_id==local_id && 
			request->src_id != local_id && (req_type == request->req_type))
		{
			found=1;
			break;
		}
	}

	if(found)
		hash_del(&entry->hlink);
	else
		request=NULL;
	spin_unlock(&htable->htable_lock);

	if(found && request->length)
		dsm_debug("hash %d", jhash(request->payload, request->length, 0));

	if(!found)
	{
		sema_up(htable, local_id, 0, req_type);//the request is for another thread
		//msleep(1);//TODO: remove me?
	}

out_err:
	*ret_request = request;
	return ret;
}

int htable_get_response(channel_htable_t *htable, int local_id, int tx_id, dsm_request_t** ret_request)
{
	int ret;
	int found;
	dsm_request_t* request;
 	struct dsm_comm_hentry *entry;

	ret = 0;
	found = 0;
	entry = NULL;
	request = NULL;

	dsm_debug("DSMFS: %s: local_id %d sema %d\n", 
				__func__, local_id, 0);
	ret=sema_down(htable, local_id, 1, -1);
	if(ret<0)
		goto out_err;

	spin_lock(&htable->htable_lock);
	dsm_debug("DSMFS: %s:%d\n", __func__, __LINE__);
	hash_for_each_possible(htable->main_htable, entry, hlink, (long) tx_id) {
	//hash_for_each(htable_lock, bkt, entry, hlink) {
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
	spin_unlock(&htable->htable_lock);

	if(found && request->length)
		dsm_debug("hash %d", jhash(request->payload, request->length, 0));

	if(!found)
	{
		sema_up(htable, local_id, 1, -1);//the request maybe for another thread
		//msleep(1);//TODO: remove me?
	}

out_err:
	*ret_request = request;
	return ret;
}


void htable_init(struct channel_htable_s *htable)
{
	int i,j;
	spin_lock_init(&htable->htable_lock);
	hash_init(htable->main_htable);
	for (i=0; i< MAX_SEMA; i++)
	{
		for (j=0; j< DSM_REQ_NUM; j++)
			sema_init(&htable->dsm_request_semaphores[i][j], 0);
		sema_init(&htable->dsm_response_semaphores[i], 0);
	}
}




void htable_drop_request(dsm_request_t* request)
{
	struct dsm_comm_hentry *entry=NULL;
	entry = container_of(request, struct dsm_comm_hentry, request);
	kfree(entry);
}
