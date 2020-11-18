# Compilation

To be compiled with a specific version of Linux found at: https://github.com/moharaka/linux-stable-dsm (branch dsmfs)


# Mounting example:

sudo mount -t dsmfs -osize=6G,mode=1777,id=$ID  tmpfs /dev/shm

where $ID is the id of the node.


# Developer Guide

## Code Entry Points:

1) filemap_fault_wrapper() and filemap_page_mkwrite_wrapper() handles page faults.
The first handles read page faults. The second handles write page faults. Both
function can be found in the file file-mmu.c.

2) There are three types of servers (threads):
	- read servers: serve read faults. Entry function: dsm_read_server
	- write servers: serve write faults. Entry function: dsm_write_server
	- invalidate servers: serve invalidate faults. Entry function: dsm_invalidate_server


## Used DSM Algorithm
The used algorithm can be found in "Memory Coherence in Shared Virtual Memory Systems" 
by K. Li and P. Hudak  [https://dl.acm.org/doi/pdf/10.1145/75104.75105] (page 16 or 336).


Algorithm 2 DynamicDistributedManager 
Read fault handler: 
 Lock( PTable[p].lock );
 ask PTable[p].probOwner for read access to p;
 PTable[ p ].probOwner := self;
 PTable[ p ].access := read;
 Unlock( PTable[ p ] .lock );

Read server: 
 Lock{ PTable[p].lock );
 IF I am owner THEN BEGIN PTable[ p ].copyset := PTableC p l.copyset U {Self};
 PTable[ p ].access := read;
 send p and PTable[ p ].copyset;
 PTable[ p ].probOwner := RequestNode;
 END ELSE BEGIN forward request to PTable[p].probOwner;
 PTable[ p l.probOwner := RequestNode;
 END;
 Unlock( PTable[ p ].lock );


Write fault handler:
 Lock{ PTable[p].lock );
 ask pTable[ p ].probOwner for write access to page p;
 Invalidate( p, PTable[ p ].copyset );
 PTable[ p ].probOwner := self;
 PTable[ p ].access := write;
 PTable[ p ] .copyset := {};
 Unlock( PTable[p] .lock ) ;
 
Write server:
 Lock{ PTable[p].lock );
 IF I am owner THEN BEGIN PTable[ p ].access := nil;
 send p and PTable[ p ].copyset;
 PTable[ p ].probOwner := RequestNode;
 END ELSE BEGIN forward request to PTable[ p ].probOwner;
 PTable[ p ].probOwner := RequestNode;
 END;
 Unlock( PTableE p ].lock );
 
Invalidate server: 
 PTable[ p ].access := nil; 
 PTable[ p ].probOwner := RequestNode;
