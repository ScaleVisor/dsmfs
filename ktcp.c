
#include "channel.h"
#include "ktcp.h"
#include "internal.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>

#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/delay.h>

struct dsm_address {
        const char *host;
        char port[8];
};

#define NDSM_CONN_THREADS 1

struct dsm_conn {
        struct socket *sock;
        struct list_head link;
	struct handling_param_s params;
        struct task_struct *threads[NDSM_CONN_THREADS];
};



static int ktcp_get_address(int server_id, int base_port, struct dsm_address *addr)
{
        if (addr == NULL) {
                return -EINVAL;
        }

        sprintf(addr->port, "%d", base_port + server_id);
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
        ret = kstrtol(port, 10, &portdec);
	if(ret)
		printk(KERN_ERR "%s", __func__);
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

        return 0;
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
        ret = listen_socket->ops->accept(listen_socket, *accept_socket, flag, true);
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
        return 0;
}

#define TEST_BUFFER_SIZE 32

static int ktcp_receive(struct socket *sock, char *buffer, size_t expected_size,
		unsigned long flags)
{
	struct kvec vec;
	int ret;
	int len = 0;
	
	struct msghdr msg = {
		.msg_name    = 0,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags   = flags | MSG_DONTWAIT,
	};

	if (expected_size == 0) {
		return 0;
	}

read_again:
	vec.iov_len = expected_size - len;
	vec.iov_base = buffer + len;
	ret = kernel_recvmsg(sock, &msg, &vec, 1, expected_size - len, flags | MSG_DONTWAIT);

	if (ret == 0) {
		return len;
	}

	// Non-blocking on the first try
	if (len == 0 && (flags & SOCK_NONBLOCK) &&
			(ret == -EWOULDBLOCK || ret == -EAGAIN)) {
		return ret;
	}

	if (ret == -EAGAIN || ret == -ERESTARTSYS) {
		goto read_again;
	}
	else if (ret < 0) {
		printk(KERN_ERR "kernel_recvmsg %d\n", ret);
		return ret;
	}
	len += ret;
	if (len != expected_size) {
		printk(KERN_WARNING "ktcp_receive receive %d bytes which expected_size=%lu bytes, read again", len, expected_size);
		goto read_again;
	}

	return len;
}


static int ktcp_handle_requests(void* data)
{
	int len=0;
	int ret = 0;
	char *buffer;
	char *new_buffer;
        char comm[TASK_COMM_LEN];
	struct dsm_conn *conn;
        struct socket* accept_sock;
	struct handling_param_s *params;
	size_t init_size;
	size_t read_more;
	dsm_channel_t *server_channel;

	conn = (struct dsm_conn*) data;
        accept_sock = (struct socket *)conn->sock;
	params = &conn->params;
	init_size=params->initial_size;
	server_channel=params->server_channel;

	buffer = kmalloc(init_size, GFP_KERNEL);//GFP_KERNEL?

        while (1) {
                if (kthread_should_stop()) {
                        ret = -EPIPE;
                        goto out;
                }


                len = ktcp_receive(accept_sock, buffer, init_size, 0);
                //BUG_ON(len > 0 && len != sizeof(struct dsm_request));

                if (len <= 0) {
                        ret = len;
                        goto out;
                }
		read_more = params->callback(buffer, init_size, NULL, server_channel);
		if(read_more)
		{
			new_buffer = kmalloc(read_more, GFP_KERNEL);
                	len = ktcp_receive(accept_sock, new_buffer, read_more, 0);
                	if (len <= 0) {
                        	ret = len;
                        	goto out;
                	}
			//kfree(new_buffer); freed by caller!?
			read_more = params->callback(buffer, init_size+read_more, new_buffer, server_channel);
		}
        }
out:
	kfree(buffer);
        get_task_comm(comm, current);
        dsm_debug("kvm[%d] %s exited server loop, error %d\n",
                                server_id, comm, ret);

        while (!kthread_should_stop()) {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule();
        }
	return 0;
}

int ktcp_release(struct socket *conn_socket)
{
	if (conn_socket == NULL) {
		return -EINVAL;
	}

	sock_release(conn_socket);

	return 0;
}

static int ktcp_create_server(void* arg)
{
        int ret;
	int server_id, base_port;
	dsm_channel_t *server_channel;
        struct socket *listen_sock = NULL;
        struct socket *accept_sock = NULL;
        struct dsm_address addr;
        struct dsm_conn *conn;
	struct list_head conn_list;
        struct task_struct *thread;
        int i, count;
        char comm[TASK_COMM_LEN];
	struct handling_param_s *params;

        allow_signal(SIGKILL);

	params = (struct handling_param_s*) arg;
	server_channel = params->server_channel;
	server_id = server_channel->id;
	base_port = server_channel->port;

        ret = ktcp_get_address(server_id, base_port, &addr);
        if (ret < 0) {
                return ret;
        }

        ret = ktcp_listen(addr.host, addr.port, &listen_sock);
        if (ret < 0) {
                return ret;
        }

        printk(KERN_INFO "server[%d] started dsm server on %s:%s\n", server_id,
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

		conn->params = *params;

                ret = ktcp_accept(listen_sock, &accept_sock, 0);
                if (ret < 0) {
                        /* We only exit with -ERESTARTSYS when the kthread should stop. */
                        if (ret == -ERESTARTSYS)
                                ret = 0;
                        goto out_listen_sock;
                }


		conn->sock = accept_sock;

		printk(KERN_INFO "server: node-%d accepted connection %p\n", server_id, conn);

                for (i = 0; i < NDSM_CONN_THREADS; i++) {
                        /*
                         * The count is somewhat meaningless since it doesn't contain
                         * information about which remote node it connects to.
                         */
                        thread = kthread_run(ktcp_handle_requests, (void*)conn, "dsm-conn/%d:%d",
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

        return ret;
}

//int ktcp_init(int server_id, ktcp_server_cb_t func)
//{
dsm_channel_t* ktcp_init(int server_id, struct super_block *sb, 
		int port, char* ip, struct handling_param_s *hparam)
{
	int ret=0;
        struct task_struct *thread;
	dsm_channel_t *server_channel = kzalloc(sizeof(dsm_channel_t), GFP_KERNEL);
	if(!server_channel)
		goto out_err;

	server_channel->id=server_id;
	server_channel->sb=sb;
	server_channel->port=port;
	server_channel->ip=ip;
	//initialize the server
	hparam->server_channel = server_channel;
	/* ingoing channel is set by the thread server */
        thread = kthread_run(ktcp_create_server,  (void*)hparam , "ktcp-server/%d:%d",
                                        server_id, 0);

	if (IS_ERR(thread)) {
		printk(KERN_ERR "%s: failed to start kernel server thread\n", __func__);
		ret = PTR_ERR(thread);
		goto out_err;
	}

	server_channel->server_thread = thread;

	printk(KERN_ERR "%s success\n", __func__);

	return server_channel;
out_err:
	printk(KERN_ERR "%s failed\n", __func__);
	return NULL;
}


/**************** CLIENT code  ****************/
static int __ktcp_send(struct socket *sock, const char *buffer, size_t length,
		unsigned long flags)
{
	struct kvec vec;
	int len, written = 0, left = length;
	int ret;

	struct msghdr msg = {
		.msg_name    = 0,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags   = flags,
	};

repeat_send:
	vec.iov_len = left;
	vec.iov_base = (char *)buffer + written;

	len = kernel_sendmsg(sock, &msg, &vec, 1, left);
	if (len == -EAGAIN || len == -ERESTARTSYS) {
		goto repeat_send;
	}
	if (len > 0) {
		written += len;
		left -= len;
		if (left != 0) {
			goto repeat_send;
		}
	}

	ret = written != 0 ? written : len;
	if (ret > 0 && ret != length) {
		printk(KERN_WARNING "ktcp_send send %d bytes which expected_size=%lu bytes", ret, length);
	}

	if (ret < 0) {
		printk(KERN_ERR "ktcp_send %d", ret);
	}

	return ret;
}

static int ktcp_client_connect(const char *host, const char *port, struct ktcp_cb *cb)
{
	int ret;
	long portdec;
	struct sockaddr_in saddr;
	struct socket *conn_socket;

	if (host == NULL || port == NULL || cb == NULL) {
		return -EINVAL;
	}

	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &conn_socket);
	if (ret < 0) {
		printk(KERN_ERR "%s: sock_create failed, return %d\n", __func__, ret);
		return ret;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	ret=kstrtol(port, 10, &portdec);
	if(ret)
		printk(KERN_ERR "%s", __func__);
	saddr.sin_port = htons(portdec);
	saddr.sin_addr.s_addr = in_aton(host);

re_connect:
	ret = conn_socket->ops->connect(conn_socket, (struct sockaddr *)&saddr,
			sizeof(saddr), O_RDWR);
	if (ret == -EAGAIN || ret == -ERESTARTSYS) {
		goto re_connect;
	}

	if (ret && (ret != -EINPROGRESS)) {
		printk(KERN_ERR "%s: connct failed, return %d\n", __func__, ret);
		sock_release(conn_socket);
		return ret;
	}

	cb->socket = conn_socket;
	mutex_init(&cb->slock);
	return 0;
}



static int __open_socket(struct ktcp_cb *cb, int dest_id, int base_port)
{
	int ret;
	struct dsm_address addr;
	
	//printk(KERN_INFO "%s requested dest %d base_port %d\n", __func__, dest_id, base_port);

	/* if already open return */
	if(cb->socket)
		return 0;

	printk(KERN_INFO "%s started dest %d base_port %d\n", __func__, dest_id, base_port);

	ret = ktcp_get_address(dest_id, base_port, &addr);
	if (ret < 0) {
		printk(KERN_ERR "kvm-dsm: address not configured properly for node-%d\n", dest_id);
		return ret;
	}

	ret = ktcp_client_connect(addr.host, addr.port, cb);
	if (ret < 0) {
		printk(KERN_ERR "kvm-dsm: node-%d failed to connect to node-%d\n",
				-1, dest_id);
		return ret;
	}
	printk(KERN_INFO "kvm-dsm: node-%d established connection with node-%d [%s:%s]\n",
			-1, dest_id, addr.host, addr.port);
	return 0;
}


int ktcp_send(int target_node_id, const char *buffer, size_t length, dsm_channel_t* server_channel)
{
	int ret;
	mm_segment_t oldmm;
	struct ktcp_cb *cb;
	cb = &server_channel->cb_channels[target_node_id];
	ret = __open_socket(cb, target_node_id, server_channel->port);
	if(ret)
		return ret;

	mutex_lock(&cb->slock);
	// Get current address access limitdo
	oldmm = get_fs();
	set_fs(KERNEL_DS);

	ret = __ktcp_send(cb->socket, buffer, length, 0);

	// Retrieve address access limit
	set_fs(oldmm);
	mutex_unlock(&cb->slock);

	return 0;
}

static int __test_send(void *arg)
{
	int port;
	char buffer[TEST_BUFFER_SIZE];
	dsm_channel_t *server_channel;

	server_channel = (dsm_channel_t*)arg;

	port = server_channel->port;

	ssleep(3);

	sprintf(buffer, "HELLO WORLD %d\n", port);

	ktcp_send(MAIN_NODE, buffer, TEST_BUFFER_SIZE, server_channel);

	return 0;
}

static void test_send(dsm_channel_t *server_channel)
{
	struct task_struct *thread;

        thread = kthread_run(__test_send, (void*)server_channel, "test_send:%d",
                                server_channel->port);
}

static size_t __test_callback(void* __buffer, size_t len, char* payload, dsm_channel_t* server_channel)
{
	char *buffer=__buffer;
	printk(KERN_INFO "%s: received buffer: %s\n", __func__, buffer);
	return 0;//next_size == 0
}

dsm_channel_t* ktcp_init_test(int server_id, struct super_block *sb, int port, char* ip)
{
	struct handling_param_s *params;
	dsm_channel_t *server_channel;

	printk(KERN_INFO "%s started ip %s port %d %p\n", __func__, ip, port, __test_callback);

	params = kzalloc(sizeof(*params), GFP_KERNEL);//TODO: embed in ...
	params->callback = __test_callback;
	params->initial_size = TEST_BUFFER_SIZE;

	server_channel = ktcp_init(server_id, sb, port, ip, params);
	if(!server_channel)
		goto err;


	test_send(server_channel);

	return server_channel;
err:
	printk(KERN_ERR "%s failed\n", __func__);
	return NULL;
}

void __dsmfs_server_destroy(struct task_struct* thread);
void ktcp_destroy_test(dsm_channel_t *server_channel)
{
	if(!server_channel)
		return;
	__dsmfs_server_destroy(server_channel->server_thread);
	//free(server_channel); TODO
	return;
}
