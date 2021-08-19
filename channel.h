#pragma once

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>
#include "config.h"


enum dsm_request_type {
	DSM_REQ_INVALIDATE = 1,
	DSM_REQ_READ = 2,
	DSM_REQ_WRITE = 3,
	DSM_REQ_TEST = 4,
	DSM_REQ_NUM = 5 /* ALWAYS last*/
};

#define __BLOCKED_HASH_BITS	7
#define MAX_SEMA MAX_NODES+1
struct channel_htable_s {
	spinlock_t htable_lock; //DEFINE_SPINLOCK(htable_lock);
	DECLARE_HASHTABLE(main_htable, __BLOCKED_HASH_BITS);

	struct semaphore dsm_request_semaphores[MAX_SEMA][DSM_REQ_NUM];
	struct semaphore dsm_response_semaphores[MAX_SEMA];
};
typedef struct channel_htable_s channel_htable_t;

struct ktcp_cb
{
	char *ip;
	int port;
	struct mutex slock;
	struct socket *socket;
};

typedef struct dsm_channel_s{
	int id;
	struct super_block *sb;
	/* input channel info */
	int port;
       	char* ip;
        struct task_struct *server_thread;
	struct channel_htable_s *htable;
	/* ouput channel info (put these arg in a separate struct) */
	struct ktcp_cb cb_channels[MAX_NODES];
}dsm_channel_t;

//TODO: synchronize with page->dsm_copyset
typedef uint32_t copyset_t;

/*
enum dsm_page_access {
	DSM_PG_INVALIDE,
	DSM_PG_REQ_READ,
	DSM_PG_REQ_WRITE
};
*/

/* Also used for response */
typedef struct dsm_request_s
{
	//requester info
	uint16_t src_id; //source node id (who built it)
	uint16_t sender_id; //source node id
	uint16_t tx_id; //internal to a node
	uint8_t req_type;	//request type

	//payload size (should be 4096 for reponses)
	uint16_t length;

	union{
		/* sender payload */
		struct{
			int ino;	// inode number (for now we assume that both FS have the same inodes ...Otherwise path!) 
			pgoff_t pg_id;	// page index in the inode
		};

		/* response payload (+ length content) */
		struct {
			copyset_t copyset;
			void* payload;
		};
	};
/* TODO: compact attribute? (but we have same arch? may not be enough: compiler version!? */
}dsm_request_t;

void print_request(dsm_request_t *request);/*dsm.c*/

dsm_channel_t* dsm_channel_create(int server_id, struct super_block *sb, int central_port, char central_ip[MAX_NODES][IP_MAX_SIZE]);

void dsm_channel_destroy(dsm_channel_t* channel);

int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request);

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type);

int dsm_channel_get_response(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type);

void dsm_drop_request(dsm_request_t* request);


/* htable function: free to use by any channel type */
int htable_get_request(channel_htable_t *,int local_id, enum dsm_request_type req_type, dsm_request_t** ret_request);
int htable_get_response(channel_htable_t* ,int local_id, int tx_id, dsm_request_t** ret_request);
int htable_put_request(channel_htable_t *, int target_id, int local_id, dsm_request_t *request);
void htable_drop_request(dsm_request_t* request);
void htable_init(channel_htable_t*);

