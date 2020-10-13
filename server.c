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
#include "util.h"
#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>

static int main_node = 0;

void print_request(dsm_request_t *request)
{
	printk(KERN_INFO "request: node_id %d tx_id %d len %d pg_idx %ld req_type %x copyset %llx\n", 
				request->nd_id,  request->tx_id,  request->length, 
					request->pg_id,  request->req_type,  request->copyset);
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

	/* page already locked */

	/* Ask owner for the page and copyset (we become owner) */
	request.nd_id=page->dsm_prob_owner;
	request.tx_id=current->pid;
	request.length=0;//no payload
	request.ino=inode->number;
	request.pg_id=page->index;
	request.req_type=DSM_REQ_READ;
	print_request(&request);

	/* Send request */
	dsm_channel_send_request(page->dsm_prob_owner, &request, NULL);

	/* Wait for response */
	dsm_channel_get_request(server_channel, &request, request.tx_id);

	BUG_ON(request.length!=PAGE_SIZE);

	page->copyset = request.copyset;

	/* Copy payload: should be a after the request structure ? */
	memcpy(page_to_virt(page), ((char*)request)+sizeof(request), PAGE_SIZE);

	/* Set flags to read only */
	dsmfs_page_ro(page);

	return 0;
}



/**********************************************************************************/

/*
 * PG_dsmfs_valid: ...
 * PG_dsmfs_write: set during mkwrite; unset: server receive reads or invalidate!
 */


int drop_write_permission(struct page *page)
{
	return try_to_unmap(page, 0);//TODO: remove just write
}

int drop_all_permission(struct page *page)
{
	return try_to_unmap(page, 0);//TODO: check SWAP_SUCCESS!
}

int forward_request(dsm_request_t *request, int target_node)
{
	dsm_channel_send_request(target_node, request, NULL);
	return 0;
}

void send_response(dsm_request_t *request, struct page* page)
{
	dsm_channel_send_request(request->nd_id, request, page_to_virt(page));
}

int __handle_read(dsm_request_t *request, struct page* page, dsm_channel_t *channel)
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

int __handle_write(dsm_request_t *request, struct page* page, dsm_channel_t *channel)
{

	drop_all_permission(page);

	//set flag to invalid
	dsmfs_page_iv(page);
	return 0;
}

struct page* dsm_get_page_locked(dsm_request_t* request, dsm_channel_t *channel)
{
	int index = request->pg_id;
	struct inode *inode= iget_locked(channel->sb, request->ino);
	struct address_space *mapping = inode->i_mapping;
	//struct page * page = find_get_page(mapping, index);
	struct page *page = pagecache_get_page(mapping, 
						index,
        					FGP_LOCK, 0);
	return page;
}

void dsm_release_page(struct page *page)
{
	unlock_page(page);
}

int is_owner(dsm_channel_t *channel, struct page *page)
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

	return channel->id == page->dsm_prob_owner;
}

int handle_request(dsm_request_t *request, dsm_channel_t *channel)
{
	int ret = 0;
	struct page *page = dsm_get_page_locked(request, channel);

	print_request(request);

	if(!page)
		forward_request(request, main_node);

	if(request->req_type == DSM_REQ_INVALIDATE)
	{
		drop_all_permission(page);
		dsmfs_page_iv(page);
		send_response(request, NULL);
	}else
	{ 	
		/* read/write */
		if(is_owner(channel, page))
		{
			if(request->req_type == DSM_REQ_READ)
				ret = __handle_read(request, page, channel);
			else
				ret = __handle_write(request, page, channel);
			/* common code to read/write */
			/* send page and copyset */
			request->copyset=page->dsm_copyset;
			send_response(request, page);
			/* set probabable owner */
			page->dsm_prob_owner = request->nd_id;
		}else
		{
			/* forward request */
			forward_request(request, page->dsm_prob_owner);
			/* set probabable owner */
			page->dsm_prob_owner = request->nd_id;
		}
	}
	dsm_release_page(page);
	return ret;
}


int dsm_server_threadfn(void *data)
{
	int ret=0;
	dsm_channel_t *server_channel=(dsm_channel_t*)data;
	dsm_request_t request;

	while(!kthread_should_stop()) 
	{
		dsm_channel_get_request(server_channel, &request, -1);
		handle_request(&request, server_channel);
	}
	
	return ret;
}


int dsmfs_server_init(int server_id, struct super_block *sb, int central_port, char* central_ip)
{
	struct task_struct *thread;
	dsm_channel_t * server_channel;
	BUG_ON(server_id < 0);
	BUG_ON(server_id >= (sizeof(copyset_t)*8));

	server_channel = dsm_channel_create(server_id, sb, central_port, central_ip);

	//TODO: use a thread pool
	thread = kthread_run(dsm_server_threadfn, (void*)server_channel, "dsm-server:%d", server_id);
	if (IS_ERR(thread)) {
		printk(KERN_ERR "DSMFS  server creation failed\n");
		return PTR_ERR(thread);
	}

	return 0;
}

