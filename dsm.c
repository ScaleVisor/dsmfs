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

void print_request(const dsm_request_t *request)
{
	dsm_debug("request: src_id %d tx_id %d len %d pg_idx %ld req_type %x copyset %x\n", 
					request->src_id,  request->tx_id,  request->length, 
					request->pg_id,  request->req_type,  request->copyset);
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
	void *dest;
	dsm_request_t request;
	const dsm_request_t *response;

	/* page already locked */
	if(is_owner(i_get_server_channel(inode), page))
		return 0;

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
	dsm_debug("");

	BUG_ON(response->length!=PAGE_SIZE);

	/* copy copyset */
	page->dsm_copyset = response->copyset;

	dest = page_to_virt(page);

	dsm_debug("dest %p src %p len %d\n", dest, response->payload, response->length);

	/* Copy payload: should be a after the request structure ? */
	memcpy(dest, (const void*)(response->payload), PAGE_SIZE);
	dsm_debug("");

	/* Set flags to read only */
	dsmfs_page_ro(page);

	/* We are the new owner */
	page->dsm_prob_owner = i_get_server_id(inode); 
	//inode->i_server_id;
	//request.src_id=i_get_server_id(inode);

	return 0;
}

static int __dsmfs_invalidate_page(struct inode *inode, struct page *page)
{
	copyset_t cs = page->dsm_copyset;
	dsm_request_t request;
	const dsm_request_t *response;
	int i;

	/* page already locked */

	/* Ask owner for the page and copyset (we become owner) */
	//request.src_id=inode->i_server_id;
	request.src_id=i_get_server_id(inode);
	request.tx_id=current->pid;
	request.length=0;//no payload
	request.ino=inode->i_ino;
	request.pg_id=page->index;
	request.req_type=DSM_REQ_INVALIDATE;
	print_request(&request);


	for(i=0; i<sizeof(cs); i++)
	{
		if(i == i_get_server_id(inode))
			continue; //don't invalidate local page

		if(cs & (1<<i))
		{
			dsm_debug("");
			/* Send request */
			dsm_channel_send_request(i_get_server_channel(inode), page->dsm_prob_owner, &request);

			dsm_debug("");
			/* Wait for response */
			dsm_channel_get_request(i_get_server_channel(inode), &response, request.tx_id);
		}
	}

	return 0;
}

int dsmfs_upgrade_page(struct inode *inode, struct page *page)
{
	dsm_request_t request;
	const dsm_request_t *response;

	/* page already locked */

	/* Ask owner for the page and copyset (we become owner) */
	request.src_id=page->dsm_prob_owner;
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

	/* invalidate all other copies */
	__dsmfs_invalidate_page(inode, page);

	/* Set flags to read only */
	dsmfs_page_rw(page);

	/* Set prob_owner to local */
	page->dsm_prob_owner = i_get_server_id(inode);

	return 0;


}


/**********************************************************************************/

/*
 * PG_dsmfs_valid: ...
 * PG_dsmfs_write: set during mkwrite; unset: server receive reads or invalidate!
 */


int drop_write_permission(struct page *page)
{
	return try_to_unmap(page, 0);//TODO: remove just write: page_mkclean(page)?
}

int drop_all_permission(struct page *page)
{
	return try_to_unmap(page, 0);//TODO: check SWAP_SUCCESS!
}

int forward_request(dsm_channel_t* channel, const dsm_request_t *request, int target_node)
{
	//request->length=0;
	dsm_channel_send_request(channel, target_node, request);
	return 0;
}

void send_response(dsm_channel_t* channel, const dsm_request_t *request, struct page* page)
{
	dsm_request_t response;
	response = *request;
	response.length=PAGE_SIZE;
	response.payload=page_to_virt(page);
	response.copyset=page->dsm_copyset;
	dsm_channel_send_request(channel, request->src_id, &response);
}

int __handle_read(const dsm_request_t *request, struct page* page, dsm_channel_t *channel)
{
	page->dsm_copyset |= channel->id; 

	/* We must be owner and so have a valid page */
	BUG_ON(PageDsmValid(page));//!handle first time page case!!!!!

	if(PageDsmWrite(page))
	{
		drop_write_permission(page);
	}

	//set flag to read only
	dsmfs_page_ro(page);
	return 0;
}

int __handle_write(const dsm_request_t *request, struct page* page, dsm_channel_t *channel)
{

	drop_all_permission(page);

	//set flag to invalid
	dsmfs_page_iv(page);
	return 0;
}

struct page* dsm_get_page_locked(const dsm_request_t* request, dsm_channel_t *channel)
{
	int index;
	struct page * page;
	struct inode *inode;
	struct address_space *mapping;

	BUG_ON(!channel);
	BUG_ON(!request);

	index = request->pg_id;
	inode = iget_locked(channel->sb, request->ino);
	mapping = inode->i_mapping;
	//struct page * page = find_get_page(mapping, index);
	//struct page *page = pagecache_get_page(mapping, index, FGP_LOCK | FGP_CREAT, 0);
	page = find_get_page_flags(mapping,
						index,
        					FGP_LOCK | FGP_CREAT);
	BUG_ON(!page);
	return page;
}

void dsm_release_page(struct page *page)
{
	unlock_page(page);
}

int handle_request(const dsm_request_t *request, dsm_channel_t *channel)
{
	int ret = 0;
	struct page *page = dsm_get_page_locked(request, channel);

	print_request(request);

	if(!page)
		forward_request(channel, request, main_node);

	if(request->req_type == DSM_REQ_INVALIDATE)
	{
		drop_all_permission(page);
		dsmfs_page_iv(page);
		dsm_channel_send_request(channel, request->src_id, request);
	}else
	{ 	
		/* read/write */
		if(is_owner(channel, page))
		{
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
			send_response(channel, request, page);
		}else
		{
			/* set probabable owner */
			page->dsm_prob_owner = request->src_id;
			/* forward request */
			forward_request(channel, request, page->dsm_prob_owner);
		}
	}
	dsm_release_page(page);
	return ret;
}


int dsm_server_threadfn(void *data)
{
	const dsm_request_t *request;
	dsm_channel_t *server_channel=(dsm_channel_t*)data;

	while(!kthread_should_stop()) 
	{
		dsm_channel_get_request(server_channel, &request, -1);
		if(!kthread_should_stop() && request)
			handle_request(request, server_channel);
		if(!request){
			dsm_debug();
		}
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
