#pragma once

#include <linux/fs.h>
#include <linux/types.h>

typedef struct dsm_channel_s{
	int id;
	struct super_block *sb;
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

#define DSM_REQ_NUM 3
enum dsm_request_type {
	DSM_REQ_INVALIDATE = 1,
	DSM_REQ_READ = 2,
	DSM_REQ_WRITE = 3
};

/* Also used for response */
typedef struct dsm_request_s
{
	//requester info
	uint16_t src_id; //source node id
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


dsm_channel_t* dsm_channel_create(int server_id, struct super_block *sb, int central_port, char* central_ip);

int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request);

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type);

void dsm_drop_request(dsm_request_t* request);
