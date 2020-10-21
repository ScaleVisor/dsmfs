/*
 * Support KVM software distributed memory
 *
 * This feature allows us to run multiple KVM instances on different machines
 * sharing the same address space.
 *
 * Authors:
 *   Chen Yubin <i@binss.me>
 *   Ding Zhuocheng <tcbbdddd@gmail.com>
 *   Zhang Jin <437629012@qq.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

/*
 * Uses 
 * dsm_channel_get_request and dsm_channel_send_request
 * dsm_get_page_locked and dsm_release_page
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
	dsm_debug("request: %p src_id %d tx_id %d len %d inode %d pg_idx %ld req_type %x copyset %x\n", 
				request, request->src_id,  request->tx_id,  request->length, 
					request->ino, request->pg_id,  request->req_type,  request->copyset);
	//dump_stack();
}

void dsmfs_page_iv(struct page *page)
{
	/* Set flags to read only */
	ClearPageDsmValid(page);
	ClearPageDsmWrite(page);
}

void dsmfs_page_ro(struct page *page)
{
	/* Set flags to read only */
	SetPageDsmValid(page);
	ClearPageDsmWrite(page);
	SetPagePinned(page);
}

void dsmfs_page_rw(struct page *page)
{
	/* Set flags to read only */
	SetPageDsmValid(page);
	SetPageDsmWrite(page);
}

int dsmfs_fill_page(struct inode *inode, struct page *page)
{
	dsm_request_t request;
	dsm_request_t *response;

	/* page already locked */
	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);

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

	dsm_debug("");
	/* Wait for response */
	dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id);
	print_request(response);
	dsm_debug("");

	BUG_ON(response->length!=PAGE_SIZE);

	/* copy copyset */
	page->dsm_copyset = response->copyset;
	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);

	/* Copy payload: should be a after the request structure ? */
	dsm_debug("page %p dest %p src %p len %d\n", page, page_to_virt(page), response->payload, response->length);
	memcpy(page_to_virt(page), (void*)(response->payload), PAGE_SIZE);
	dsm_debug("");


	/* We are the new owner */
	page->dsm_prob_owner = i_get_server_id(inode); 
	//inode->i_server_id;
	//request.src_id=i_get_server_id(inode);

	/* drop request */
	dsm_drop_request(response);

out:
	/* Set flags to read only */
	dsmfs_page_ro(page);

	dsm_debug("page %p inode %p index %ld copyset %d\n", page, inode, page->index, page->dsm_copyset);

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


	for(i=0; i<(sizeof(cs)*8); i++)
	{
		dsm_debug("current id is %d, clearing node %d", i_get_server_id(inode), i);
		if(i == i_get_server_id(inode))
			continue; //don't invalidate local page

		dsm_debug("cs value %x, testing agaist %x, result %d\n", cs, (1<<i), cs & (1<<i));

		if(cs & (1<<i))
		{
			dsm_debug("");
			/* Send request */
			dsm_channel_send_request(i_get_server_channel(inode), i, &request);

			dsm_debug("");
			/* Wait for response */
			dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id);
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

	/* page already locked */
	dsm_debug("");

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
	dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id);
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
	page->dsm_prob_owner = i_get_server_id(inode);

	if(response)
		dsm_drop_request(response);

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

	//page->dsm_copyset |= (1 << channel->id); we are dropping all permissions! no need

	drop_all_permission(page);

	//set flag to invalid
	dsmfs_page_iv(page);
	return 0;
}

struct page* dsm_get_page_locked(dsm_request_t* request, dsm_channel_t *channel)
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
	dsm_debug("sb %p inode %p num %ld, size %lld, state %ld!\n", inode->i_sb, inode, inode->i_ino, inode->i_size, inode->i_state);
	BUG_ON(inode->i_state & I_NEW);
	mapping = inode->i_mapping;
	//struct page * page = find_get_page(mapping, index);
	//struct page *page = pagecache_get_page(mapping, index, FGP_LOCK | FGP_CREAT, 0);
	fgp_flags=FGP_LOCK;
	if(channel->id == main_node)
		fgp_flags|=FGP_CREAT;//main_nde must have the page or allocate it
	page = find_get_page_flags(mapping, index, fgp_flags);
	if(channel->id == main_node)
	{
		BUG_ON(!page);
		dsmfs_page_ro(page);
	}
	BUG_ON(!page);
	return page;
}

void dsm_release_page(struct page *page)
{
	unlock_page(page);
}

int handle_request(dsm_request_t *request, dsm_channel_t *channel)
{
	int ret = 0;
	struct page *page = dsm_get_page_locked(request, channel);

	print_request(request);

	if(!page)
	{
		forward_request(channel, request, main_node);
		goto out;
	}

	if(request->req_type == DSM_REQ_INVALIDATE)
	{
		dsm_debug("Handle invalidate request %x\n", request->req_type);
		drop_all_permission(page);
		dsmfs_page_iv(page);
		dsm_channel_send_request(channel, request->src_id, request);
	}else
	{ 	
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
			page->dsm_prob_owner = request->src_id;
			/* send page */
			send_response(channel, request, page);//FIXME: response allocated on the stack!!!!!!!!!
		}else
		{
			/* set probabable owner */
			page->dsm_prob_owner = request->src_id;
			/* forward request */
			forward_request(channel, request, page->dsm_prob_owner);
		}
	}
	dsm_release_page(page);
out:
	return ret;
}


int dsm_server_threadfn(void *data)
{
	dsm_request_t *request;
	dsm_channel_t *server_channel=(dsm_channel_t*)data;

	while(!kthread_should_stop()) 
	{
		dsm_channel_get_request(server_channel, &request, -1);
		if(!kthread_should_stop() && request)
			handle_request(request, server_channel);
		if(!request){
			dsm_debug("");
		}
		dsm_drop_request(request);
	}
	
	return 0;
}

//TODO: the argument should be fsi ? or another specific struct
int dsmfs_server_init(struct super_block *sb)
{
	dsm_channel_t * server_channel;
	struct dsmfs_fs_info *fsi;
	int server_id;
	
	fsi = (struct dsmfs_fs_info*) sb->s_fs_info;
	server_id = fsi->server_id;

	BUG_ON(server_id < 0);
	BUG_ON(server_id >= (sizeof(copyset_t)*8));

	server_channel = dsm_channel_create(server_id, sb, fsi->rport, fsi->rip);

	fsi->server_channel = server_channel;

	dsm_debug("%s: server_id %d\n", __func__, server_channel->id);

	/* TODO: use a thread pool */
	fsi->thread = kthread_run(dsm_server_threadfn, (void*)server_channel, "dsm-server:%d", server_id);
	if (IS_ERR(fsi->thread)) {
		dsm_print("server creation failed\n");
		return PTR_ERR(fsi->thread);
	}

	return 0;
}

void dsmfs_server_destroy(struct dsmfs_fs_info *fsi)
{

	dsm_print("DSMFS: killing server\n");
	if (fsi->thread)
	{
		//TODO: send signal? More thinking on the stopping phase
       		kthread_stop(fsi->thread);
		dsm_print("DSMFS: THREAD Stopped\n");

	}
}
