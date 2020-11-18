/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */

/*
 * Uses 
 * dsm_channel_get_request and dsm_channel_send_request
 * dsm_get_page and dsm_release_page
 * drop_all_permission and drop_write_permission
 */

#include "channel.h"
#include "internal.h"
#include "util.h"
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/rmap.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>

static int main_node = 0;


	
#define i_get_server_id(__inode) (((struct dsmfs_fs_info*)__inode->i_sb->s_fs_info)->server_id)
#define i_get_server_channel(__inode) (((struct dsmfs_fs_info*)__inode->i_sb->s_fs_info)->server_channel)

static int is_owner(dsm_channel_t *channel, struct page *page)
{
	/*
	 * We are also owner for the first time a page
	 * is loaded and we are the node main_node(0).
	 * is 'dsm_prob_owner' set to '0' the first
	 * arround ? We assume yes! (TO BE CHECKED!!!)
	 * For the other times, since we pin the pages
	 * the sate of dsm_prob_owner should be corre-
	 * -ctly set.
	 */
	BUG_ON(!channel);
	BUG_ON(!page);
	return channel->id == page->dsm_prob_owner;
}

void print_request(dsm_request_t *request)
{
	dsm_debug("request: %p src_id %d tx_id %d len %d inode %d pg_idx %ld req_type %x payload %p copyset %x\n", 
				request, request->src_id,  request->tx_id,  request->length, 
					request->ino, request->pg_id,  request->req_type, request->payload,  request->copyset);
	//dump_stack();
}

void dsmfs_page_iv(struct page *page)
{
	/* Set flags to read only */
	ClearPageDsmValid(page);
	ClearPageDsmWrite(page);
	dsm_debug("clear both bits %p %ld\n", page, page->index);
}

void dsmfs_page_ro(struct page *page)
{
	/* Set flags to read only */
	SetPageDsmValid(page);
	ClearPageDsmWrite(page);
	//SetPagePinned(page);
	dsm_debug("RO bits %p %ld\n", page, page->index);
}

void dsmfs_page_rw(struct page *page)
{
	/* Set flags to read only */
	SetPageDsmValid(page);
	SetPageDsmWrite(page);
	dsm_debug("RW bits %p %ld\n", page, page->index);
}

int dsmfs_fill_page(struct inode *inode, struct page *page)
{
	dsm_request_t request;
	dsm_request_t *response;
	dsm_time("Entered");

	BUG_ON(!page);
	BUG_ON(!PageLocked(page));

	/* page already locked */
	dsm_debug("page %p page %p inode %p index %ld copyset %d\n", page, page_to_virt(page), inode, page->index, page->dsm_copyset);

	/* if we are already owner */
	if(is_owner(i_get_server_channel(inode), page))
		goto out;

	/* Ask owner for the page and copyset (we become owner) */
	request.src_id=i_get_server_id(inode);
	request.tx_id=current->pid;
	request.length=0;//no payload
	request.ino=inode->i_ino;
	request.pg_id=page->index;
	request.req_type=DSM_REQ_READ;
	print_request(&request);

	/* Send request */
	dsm_channel_send_request(i_get_server_channel(inode), page->dsm_prob_owner, &request);

	dsm_debug("\n");
	/* Wait for response */
	dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id, 0);
	print_request(response);
	dsm_debug("\n");

	BUG_ON(response->length!=PAGE_SIZE);

	/* copy copyset */
	page->dsm_copyset = response->copyset;
	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);

	/* Copy payload: should be a after the request structure ? */
	dsm_debug("page %p dest %p src %p len %d\n", page, page_to_virt(page), response->payload, response->length);
	memcpy(page_to_virt(page), (void*)(response->payload), PAGE_SIZE);
	dsm_debug("\n");

	if(response->length)
		dsm_debug("content int0 %d\n", *((int*)(page_to_virt(page))));

	/* We are the new owner */
	BUG_ON(!PageLocked(page));
	page->dsm_prob_owner = i_get_server_id(inode); 
	//inode->i_server_id;
	//request.src_id=i_get_server_id(inode);

	/* drop request */
	dsm_drop_request(response);

out:
	/* Set flags to read only */
	dsmfs_page_ro(page);

	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);

	dsm_time("Exited");
	return 0;
}

static int __dsmfs_invalidate_page(struct inode *inode, struct page *page)
{
	copyset_t cs = page->dsm_copyset;
	dsm_request_t request;
	dsm_request_t *response;
	int i;

	/* page already locked */
	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);


	/* Ask owner for the page and copyset (we become owner) */
	//request.src_id=inode->i_server_id;
	request.src_id=i_get_server_id(inode);
	request.tx_id=current->pid;
	request.length=0;//no payload
	request.ino=inode->i_ino;
	request.pg_id=page->index;
	request.req_type=DSM_REQ_INVALIDATE;
	print_request(&request);


	for(i=0; i<(sizeof(cs)*8) && cs; i++, cs>>=1)
	{
		dsm_debug("current id is %d, clearing node %d", i_get_server_id(inode), i);
		if(i == i_get_server_id(inode))
			continue; //don't invalidate local page

		dsm_debug("cs value %x, result %d\n", cs, cs&1);

		if(cs & 1)
		{
			dsm_debug("");
			/* Send request */
			dsm_channel_send_request(i_get_server_channel(inode), i, &request);

			dsm_debug("");
			/* Wait for response */
			dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id, 0);
			dsm_drop_request(response);
			
			//unsetting the bit in the page
			page->dsm_copyset&=~(1<<i);
		}
	}

	return 0;
}

int dsmfs_upgrade_page(struct inode *inode, struct page *page)
{
	dsm_request_t request;
	dsm_request_t *response;
	response=NULL;
	dsm_time("Entered");

	/* page already locked */
	dsm_debug("\n");

	BUG_ON(!PageLocked(page));

	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);
	/* if we are already owner */
	if(is_owner(i_get_server_channel(inode), page))
		goto inval;

	/* Ask owner for the page and copyset (we become owner) */
	request.src_id=i_get_server_id(inode);
	request.tx_id=current->pid;
	request.length=0;//no payload
	request.ino=inode->i_ino;
	request.pg_id=page->index;
	/* TODO: sometimes we don't need the page content, create a new request */
	request.req_type=DSM_REQ_WRITE;
	print_request(&request);

	dsm_debug("");
	/* Send request */
	dsm_channel_send_request(i_get_server_channel(inode), page->dsm_prob_owner, &request);

	dsm_debug("");
	/* Wait for response */
	dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id, 0);
	dsm_debug("");

	BUG_ON(response->length!=PAGE_SIZE);

	/* copy copyset */
	page->dsm_copyset = response->copyset;

	/* Copy payload */
	memcpy(page_to_virt(page), response->payload, PAGE_SIZE);

inval:
	dsm_debug("calling inval");
	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);
	/* invalidate all other copies */
	__dsmfs_invalidate_page(inode, page);

	/* Set flags to read only */
	dsmfs_page_rw(page);

	/* Set prob_owner to local */
	BUG_ON(!PageLocked(page));
	page->dsm_prob_owner = i_get_server_id(inode);

	if(response)
		dsm_drop_request(response);

	dsm_time("Exited");
	return 0;
}


/**********************************************************************************/

/*
 * PG_dsmfs_valid: ...
 * PG_dsmfs_write: set during mkwrite; unset: server receive reads or invalidate!
 */


int dsm_page_unmap(struct page *page, int clear_read);
int drop_write_permission(struct page *page)
{
	int ret;
	ret=dsm_page_unmap(page, 0);
	//ret=try_to_unmap(page, 0);//TODO: remove just write: page_mkclean(page)?
	dsm_debug("drop write permissions %ld return %d\n", page->index, ret);
	return ret;
}

int drop_all_permission(struct page *page)
{
	int ret;
	ret=dsm_page_unmap(page, 1);
	//ret=try_to_unmap(page, 0);//TODO: check SWAP_SUCCESS!
	dsm_debug("drop all permissions %ld return %d\n", page->index, ret);
	return ret;
}

int forward_request(dsm_channel_t* channel, dsm_request_t *request, int target_node)
{
	//request->length=0;
	dsm_channel_send_request(channel, target_node, request);
	return 0;
}

void send_response(dsm_channel_t* channel, dsm_request_t *request, struct page* page)
{
	dsm_request_t response;
	response = *request;
	response.length=PAGE_SIZE;
	response.payload=page_to_virt(page);
	response.copyset=page->dsm_copyset;
	dsm_channel_send_request(channel, request->src_id, &response);
}

int __handle_read(dsm_request_t *request, struct page* page, dsm_channel_t *channel)
{
	dsm_debug("Handle read request\n");

	page->dsm_copyset |= (1 << channel->id); 

	/* We must be owner and so have a valid page */
	BUG_ON(!PageDsmValid(page));

	if(PageDsmWrite(page))
	{
		drop_write_permission(page);
	}

	//set flag to read only
	dsmfs_page_ro(page);
	return 0;
}

int __handle_write(dsm_request_t *request, struct page* page, dsm_channel_t *channel)
{

	dsm_debug("Handle write request\n");

	page->dsm_copyset &= ~(1 << channel->id); //we are dropping all permissions! no need to receive inval

	drop_all_permission(page);

	//set flag to invalid
	dsmfs_page_iv(page);
	return 0;
}

struct page* dsm_get_page(dsm_request_t* request, dsm_channel_t *channel, int locked)
{
	int index;
	int fgp_flags;
	struct page * page;
	struct inode *inode;
	struct address_space *mapping;

	BUG_ON(!channel);
	BUG_ON(!request);

	index = request->pg_id;
	inode = iget_locked(channel->sb, request->ino);
	dsm_debug("sb %p inode %p num %ld, pg_idx %d size %lld, state %ld, locked %d\n", 
			inode->i_sb, inode, inode->i_ino, index, inode->i_size, inode->i_state, locked);
	BUG_ON(inode->i_state & I_NEW);
	mapping = inode->i_mapping;
	//struct page * page = find_get_page(mapping, index);
	//struct page *page = pagecache_get_page(mapping, index, FGP_LOCK | FGP_CREAT, 0);
	fgp_flags=0;
	if(locked)
		fgp_flags=FGP_LOCK;
	page = find_get_page_flags(mapping, index, fgp_flags);

	//node 0 should force the allocation of a new page
	if(!page && channel->id == main_node)
	{
		BUG_ON(!locked);
		fgp_flags|=FGP_CREAT;//main_nde must have the page or allocate it
		page = find_get_page_flags(mapping, index, fgp_flags);
		BUG_ON(!page);
		dsmfs_page_ro(page);//default rights
	}
	if(locked)
		dsm_debug("locked page index %d owner %d\n", index, page->dsm_prob_owner);
	//BUG_ON(!page);
	//FIXME: here? yes, we have a refcount on the page it is enough?!
	//iput(inode);
	return page;
}

void dsm_release_page(struct page *page, int locked)
{
	//put_page(page);//FIXME: should we not lock the page
	if(locked)
	{
		dsm_debug("unlocked page index %ld owner %d\n", page->index, page->dsm_prob_owner);
		unlock_page(page);
	}
}

int handle_request(dsm_request_t *request, dsm_channel_t *channel)
{
	int ret = 0;
	int locked;
	struct page *page;
	if(request->req_type == DSM_REQ_READ)
		dsm_time("Entered READ");
	else if(request->req_type == DSM_REQ_WRITE)
		dsm_time("Entered WRITE");
	else
		dsm_time("Entered INVAL");

	print_request(request);
	
	locked = !(request->req_type == DSM_REQ_INVALIDATE);//inval does not lock page
	page = dsm_get_page(request, channel, locked);

	if(!page)
	{
		BUG_ON(request->req_type != DSM_REQ_INVALIDATE);//if we receive an invalidate we must have the page
		dsm_debug("Forward request type %x\n", request->req_type);
		forward_request(channel, request, main_node);
		goto out;
	}

	if(request->req_type == DSM_REQ_INVALIDATE)
	{
		dsm_debug("Handle invalidate request %x\n", request->req_type);
		BUG_ON(is_owner(channel, page));
		drop_all_permission(page);
		dsmfs_page_iv(page);
		dsm_channel_send_request(channel, request->src_id, request);
	}else
	{ 	
		BUG_ON(!PageLocked(page));

		/* read/write */
		if(is_owner(channel, page))
		{
			dsm_debug("Handle read/write request %x\n", request->req_type);
			if(request->req_type == DSM_REQ_READ)
				ret = __handle_read(request, page, channel);
			else
				ret = __handle_write(request, page, channel);

			/*** common code to read/write ***/
			/* send page and copyset */
			//request->copyset=page->dsm_copyset;
			/* set probabable owner */
			BUG_ON(!PageLocked(page));
			page->dsm_prob_owner = request->src_id;
			/* send page */
			send_response(channel, request, page);
		}else
		{
			dsm_debug("Forward request type %x\n", request->req_type);
			BUG_ON(page->dsm_prob_owner == request->src_id); /* forward to local node ?*/
			/* forward request */
			forward_request(channel, request, page->dsm_prob_owner);
			/* set probabable owner */
			BUG_ON(!PageLocked(page));
			page->dsm_prob_owner = request->src_id;
		}
	}
	dsm_release_page(page, locked);
out:
	dsm_time("Exited");
	return ret;
}

int dsm_core_server(void *data, enum dsm_request_type req_type)
{
	dsm_request_t *request;
	dsm_channel_t *server_channel=(dsm_channel_t*)data;

	/* set signal mask to what we want to respond */
	allow_signal(SIGKILL);

	while(!kthread_should_stop()) 
	{
		dsm_channel_get_request(server_channel, &request, -1, req_type);
		if(!request)
			continue;
		handle_request(request, server_channel);
		dsm_drop_request(request);
	}
	
	return 0;
}


int dsm_read_server(void *data)
{
	return dsm_core_server(data, DSM_REQ_READ);
}

int dsm_write_server(void *data)
{
	return dsm_core_server(data, DSM_REQ_WRITE);
}

int dsm_inval_server(void *data)
{
	return dsm_core_server(data, DSM_REQ_INVALIDATE);
}

//TODO: the argument should be fsi ? or another specific struct
int dsmfs_server_init(struct super_block *sb)
{
	dsm_channel_t * server_channel;
	struct dsmfs_fs_info *fsi;
	int server_id;
	int i;
	
	fsi = (struct dsmfs_fs_info*) sb->s_fs_info;
	server_id = fsi->server_id;

	BUG_ON(server_id < 0);
	BUG_ON(server_id >= (sizeof(copyset_t)*8));

	server_channel = dsm_channel_create(server_id, sb, fsi->rport, fsi->rip);

	fsi->server_channel = server_channel;

	dsm_debug("%s: server_id %d\n", __func__, server_channel->id);

	/* TODO: use a thread pool */
	for(i=0; i<NUM_SERVER; i++)
	{
		fsi->read_server[i] = kthread_run(dsm_read_server, (void*)server_channel, "dsm-read-server:%d:%d", server_id, i);
		if (IS_ERR(fsi->read_server[i])) {
			dsm_print("server creation failed\n");
			return PTR_ERR(fsi->read_server[i]);
		}
		fsi->write_server[i] = kthread_run(dsm_write_server, (void*)server_channel, "dsm-write-server:%d:%d", server_id, i);
		if (IS_ERR(fsi->write_server[i])) {
			dsm_print("server creation failed\n");
			return PTR_ERR(fsi->write_server[i]);
		}
		fsi->inval_server[i] = kthread_run(dsm_inval_server, (void*)server_channel, "dsm-inval-server:%d:%d", server_id, i);
		if (IS_ERR(fsi->inval_server[i])) {
			dsm_print("server creation failed\n");
			return PTR_ERR(fsi->inval_server[i]);
		}
	}

	return 0;
}

void __dsmfs_server_destroy(struct task_struct* thread)
{
	if (thread)
	{
		send_sig(SIGKILL, thread, 1);
       		kthread_stop(thread);
		dsm_print("THREAD Stopped\n");
	}
}

void dsmfs_server_destroy(struct dsmfs_fs_info *fsi)
{
	int i;
	dsm_print("killing server\n");
	for(i=0; i<NUM_SERVER; i++)
	{
		__dsmfs_server_destroy(fsi->read_server[i]);
		__dsmfs_server_destroy(fsi->write_server[i]);
		__dsmfs_server_destroy(fsi->inval_server[i]);
	}
}
