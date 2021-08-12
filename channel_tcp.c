

struct dsm_address {
        const char *host;
        char port[8];
};

#define NDSM_CONN_THREADS 1

struct dsm_conn {
        struct socket *sock;
        struct list_head link;
        struct task_struct *threads[NDSM_CONN_THREADS];
};

static int get_dsm_address(int server_id, struct dsm_address *addr)
{
        if (addr == NULL) {
                return -EINVAL;
        }

        sprintf(addr->port, "%d", 37710 + dsm_id);
        addr->host = "127.0.0.1"; //TODO: as a parameter of module

        return 0;
}

static int ktcp_listen(const char *host, const char *port, struct socket **listen_socket)
{
        int ret;
        struct sockaddr_in saddr;
	#define DEFAULT_BACKLOG 16
        long portdec;

        ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, listen_socket);
        if (ret != 0) {
                printk(KERN_ERR "sock_create %d", ret);
                return ret;
        }
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        kstrtol(port, 10, &portdec);
        saddr.sin_port = htons(portdec);
        saddr.sin_addr.s_addr = in_aton(host);

        ret = (*listen_socket)->ops->bind(*listen_socket, (struct sockaddr *)&saddr, sizeof(saddr));
        if (ret != 0) {
                printk(KERN_ERR "bind %d\n", ret);
                sock_release(*listen_socket);
                return ret;
        }

        ret = (*listen_socket)->ops->listen(*listen_socket, DEFAULT_BACKLOG);
        if (ret != 0) {
                printk(KERN_ERR "listen %d\n", ret);
                sock_release(*listen_socket);
                return ret;
        }

        return SUCCESS;
}

int ktcp_accept(struct socket *listen_socket, struct socket **accept_socket, unsigned long flag)
{
        int ret;

        if (listen_socket == NULL) {
                printk(KERN_ERR "null listen_socket\n");
                return -EINVAL;
        }

        ret = sock_create_lite(listen_socket->sk->sk_family, listen_socket->sk->sk_type,
                        listen_socket->sk->sk_protocol, accept_socket);
        if (ret != 0) {
                printk(KERN_ERR "sock_create %d\n", ret);
                return ret;
        }

re_accept:
        ret = listen_socket->ops->accept(listen_socket, *accept_socket, flag);
        if (ret == -ERESTARTSYS) {
                if (kthread_should_stop())
                        return ret;
                goto re_accept;
        }
        // When setting SOCK_NONBLOCK flag, accept return this when there's nothing in waiting queue.
        if (ret == -EWOULDBLOCK || ret == -EAGAIN) {
                sock_release(*accept_socket);
                *accept_socket = NULL;
                return ret;
        }
        if (ret < 0) {
                printk(KERN_ERR "accept %d\n", ret);
                sock_release(*accept_socket);
                *accept_socket = NULL;
                return ret;
        }

        (*accept_socket)->ops = listen_socket->ops;
        return SUCCESS;
}

static void* channel_handle_requests(void* arg)
{
	int ret = 0, idx;

        struct dsm_request_t req;
        bool retry = false;
        char comm[TASK_COMM_LEN];
        struct socket* accept_sock = (struct socket *)data;

        while (1) {
                if (kthread_should_stop()) {
                        ret = -EPIPE;
                        goto out;
                }

                len = ktcp_receive(accept_sock, &req);
                BUG_ON(len > 0 && len != sizeof(struct dsm_request));

                if (len <= 0) {
                        ret = len;
                        goto out;
                }
		/* place in hash table */
		htable_put_request(target_node, server_channel->id, request);
        }
out:
        get_task_comm(comm, current);
        dsm_debug("kvm[%d] %s exited server loop, error %d\n",
                                server_id, comm, ret);

        while (!kthread_should_stop()) {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule();
        }
	return NULL;
}


static void* dsm_tcp_create_server(void* arg)
{
        int ret;
	int server_id
	dsm_channel_t *server_channel;
        struct socket *listen_sock = NULL;
        struct socker *accept_sock = NULL;
        struct dsm_address addr;
        struct dsm_conn *conn;
        struct task_struct *thread;
        int i, count;
        char comm[TASK_COMM_LEN];

        allow_signal(SIGKILL);

	server_channel = (dsm_channel_t*) arg;
	server_id = server_channel->id;

        ret = get_dsm_address(server_id, &addr);
        if (ret < 0) {
                return ret;
        }

        ret = ktcp_listen(addr.host, addr.port, &listen_sock);
        if (ret < 0) {
                return (void*)(long) ret;
        }

        dsm_debug_v("server[%d] started dsm server on %s:%s\n", server_id,
                        addr.host, addr.port);

        count = 0;
        while (1) {
                if (kthread_should_stop()) {
                        ret = 0;
                        goto out_listen_sock;
                }

                conn = kmalloc(sizeof(struct dsm_conn), GFP_KERNEL);
                if (conn == NULL) {
                        ret = -ENOMEM;
                        goto out_listen_sock;
		}

                ret = ktcp_accept(listen_sock, &accept_sock, 0);
                if (ret < 0) {
                        /* We only exit with -ERESTARTSYS when the kthread should stop. */
                        if (ret == -ERESTARTSYS)
                                ret = 0;
                        goto out_listen_sock;
                }

		printk(KERN_INFO "server: node-%d accepted connection\n", server_id);

		conn->socket = accept_sock;

                for (i = 0; i < NDSM_CONN_THREADS; i++) {
                        /*
                         * The count is somewhat meaningless since it doesn't contain
                         * information about which remote node it connects to.
                         */
                        thread = kthread_run(channel_handle_requests, (void*)accept_sock, "dsm-conn/%d:%d",
                                        server_id, count++);
                        if (IS_ERR(thread)) {
                                printk(KERN_ERR "kvm-dsm: failed to start kernel thread for dsm connection\n");
                                ret = PTR_ERR(thread);
        			ktcp_release(accept_sock);
                                goto out_listen_sock;
                        }
                        conn->threads[i] = thread;
                }
                list_add_tail(&conn->link, &conn_list);
        }

out_listen_sock:
        while (!list_empty(&conn_list)) {
                conn = list_first_entry(&conn_list, struct dsm_conn, link);
                list_del(&conn->link);
                for (i = 0; i < NDSM_CONN_THREADS; i++) {
                        get_task_comm(comm, conn->threads[i]);
                        send_sig(SIGKILL, conn->threads[i], 1);
                        ret = kthread_stop(conn->threads[i]);
                        dsm_debug("kvm[%d] dsm connection thread %s exited with %d",
                                        server_id, comm, ret);
                }
                ktcp_release(conn->sock);
                kfree(conn);
        }

        ktcp_release(listen_sock);

        return (void*) (long)ret;
}

void dsm_channel_init(int local_id)
{

}

dsm_channel_t* dsm_channel_create(int server_id, struct super_block *sb, int central_port, char* central_ip)
{

	dsm_channel_t *server_channel = kmalloc(sizeof(dsm_channel_t), GFP_KERNEL);
	server_channel->id=server_id;
	server_channel->sb=sb;
	//server_channel->central_port=central_port;
	//server_channel->central_ip=central_port;
	
        server_channel->server_thread = kthread_run(dsm_tcp_create_server, (void*)server_channel, "dsm-conn/%d",
                                        server_id);

	dsm_channel_init(server_id);

	return server_channel;

}

int dsm_channel_send_request(dsm_channel_t* server_channel, int target_node, dsm_request_t* request)
{
	printk(KERN_INFO "%s: No yet implemented", __func__);

}

int dsm_channel_get_request(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	/* should be called by dsm servers only? */
	int ret = 0;
	BUG_ON(tx_id!=-1);
	dsm_request_t * req=NULL;
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);
		ret=htable_get_request(server_channel->id, req_type, &req);
	}while(!ret && req==NULL); 

	*request=req;
	return ret;

}

int dsm_channel_get_response(dsm_channel_t* server_channel, dsm_request_t** request, int tx_id, enum dsm_request_type req_type)
{
	/* req_type ignored for now */
	int ret = 0;
	BUG_ON(tx_id==-1);
	dsm_request_t * req=NULL;
	do{
		dsm_debug("server_id %d tx_id %d\n", server_channel->id, tx_id);
		ret = htable_get_response(server_channel->id, tx_id, &req);
	}while(req==NULL); /* main difference with get_request : merge ? */

	*request=req;

	return ret;
}

void dsm_drop_request(dsm_request_t* request)
{
	printk(KERN_INFO "%s: No yet implemented", __func__);
}
