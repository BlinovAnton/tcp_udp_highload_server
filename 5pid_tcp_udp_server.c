#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <mqueue.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define QUEUE_SIZE 50
#define LISTEN_QUEUE 5
#define MAX_IP_LENGTH 15
#define COND_MSG_SIZE 100
#define TIME 1
#define PRINT_PID getpid() == main_pid ? 40 : 91, getpid(), getpid() == main_pid ? 41 : 93

mqd_t mq_udp, mq_tcp;
pid_t main_pid = 0, tcp_pid = 0;
pthread_t *tids = NULL;
int start_pid_cer = 0, pids_num = 3, pop_val = 0;
int tcp_listen_fd = 0, tcp_accept_fd = 0, udp_socket = 0;
const char *mq_udp_name = "/mq_udp_file";
const char *mq_tcp_name = "/mq_tcp_file";
struct thread_info *tinfo = NULL;


struct mq_buf {
    int fd;
    struct sockaddr_in cli_sa_in;
};

struct thread_info {
    int tnum;
    struct sockaddr_in ser_sa_in;
    struct mq_attr mq_tattr;
    char *tmessage;
    int tmsg_len;
};

void sigint_hand (int);

void cleanup_handler (void *arg) {
    if (mq_close(mq_tcp)       == -1) printf("%c%d%c mq_close(tcp) for nothing\n", PRINT_PID);
    if (mq_unlink(mq_tcp_name) == -1) printf("%c%d%c mq_unlink(tcp) for nothing\n", PRINT_PID);
    printf("%c%d%c end handler\n", PRINT_PID);
}

void *tcp_thread (void *arg) {
    int bytes = 0, mq_buf_len = sizeof(struct mq_buf);

    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0) {
	printf("%c%d%c.%d pthread_setcancelstate(disable) fault\n", PRINT_PID, tinfo->tnum);
	perror(":::");
	pthread_exit(0);
    }

    struct mq_buf mq_tcp_msg;
    struct thread_info *tinfo = (struct thread_info *)arg;
    char *message = NULL;
    char *server_ip = inet_ntoa(tinfo->ser_sa_in.sin_addr), *cli_ip = NULL;
    short server_port = ntohs(tinfo->ser_sa_in.sin_port), cli_port = 0;
    char buf_for_recv[COND_MSG_SIZE];

    message = strndup(tinfo->tmessage, tinfo->tmsg_len);
    if (message == NULL) {
	printf("%c%d%c.%d strdup() fault\n", PRINT_PID, tinfo->tnum);
	perror(":::");
	pthread_exit(0);
    }
    printf("%c%d%c.%d %s:%d\n", PRINT_PID, tinfo->tnum, server_ip, server_port);
    mq_tcp = mq_open(mq_tcp_name, O_RDWR, S_IRWXU, &tinfo->mq_tattr);
    if (mq_tcp == -1) {
        printf("%c%d%c mq_open() fault\n", PRINT_PID);
        perror(":::");
        pthread_exit(0);
    }

    pthread_cleanup_push(cleanup_handler, NULL);
    if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0) {
	printf("%c%d%c.%d pthread_setcancelstate(enable) fault\n", PRINT_PID, tinfo->tnum);
	perror(":::");
	pthread_exit(0);
    }

    while (1) {
	bytes = mq_receive(mq_tcp, (char *)&mq_tcp_msg, mq_buf_len, 0);
	if (bytes < mq_buf_len) {
	    printf("%c%d%c.%d mq_receive(%d/%d received) fault\n",
			PRINT_PID, tinfo->tnum, bytes, mq_buf_len);
	    perror(":::");
	    pthread_exit(0);
	}
	cli_ip = inet_ntoa(mq_tcp_msg.cli_sa_in.sin_addr);
	cli_port = mq_tcp_msg.cli_sa_in.sin_port;
	printf("%c%d%c.%d new client %s:%d\n", PRINT_PID, tinfo->tnum, cli_ip, ntohs(cli_port));
	if ((bytes = recv(mq_tcp_msg.fd, buf_for_recv, COND_MSG_SIZE, 0)) <= 0) {
	    printf("%c%d%c.%d recv() from %s fault\n", PRINT_PID, tinfo->tnum, cli_ip);
	    perror(":::");
	    pthread_exit(0);
	} else {
	    printf("%c%d%c.%d recv (%d bytes) from client(%s:%d): %s\n",
			PRINT_PID, tinfo->tnum, bytes, cli_ip, ntohs(cli_port), buf_for_recv);
	}
	#if TIME
	sleep(10);
	#endif
	if ((bytes = send(mq_tcp_msg.fd, message, tinfo->tmsg_len, 0)) <= 0) {
	    printf("%c%d%c.%d send() to %s fault\n", PRINT_PID, tinfo->tnum, cli_ip);
	    perror(":::");
	    pthread_exit(0);
	} else {
	    printf("%c%d%c.%d send (%d bytes) to client(%s:%d)\n",
		    PRINT_PID, tinfo->tnum, bytes, cli_ip, ntohs(cli_port));
	}
    }

    pthread_cleanup_pop(0);

}

int main () {
    int i = 0, port = 8080, bytes = 0, mq_buf_len = sizeof(struct mq_buf);
    int  bind_res = 0, res = 0, sa_in_len = sizeof(struct sockaddr_in);
    unsigned short udp_port = 0;
    struct sockaddr_in server_info, cli_info;
    struct mq_buf mq_udp_msg, mq_tcp_msg;
    struct mq_attr mq_any_attr;
    int jopa = 0;
    char buf_for_recv[COND_MSG_SIZE];
    char *server_ip = NULL;
    pid_t pid = 0;
    void *dest = NULL, *src = NULL;
    char message[] = "Alloha from server!";
    char server_address[][MAX_IP_LENGTH] = {"192.168.0.2", "10.0.2.15", "127.0.0.1"};

    struct sigaction new_act, old_act;
    new_act.sa_handler = sigint_hand;
    sigemptyset(&new_act.sa_mask);
    new_act.sa_flags = 0;
    sigaction(SIGINT, NULL, &old_act);
    if (old_act.sa_handler != SIG_IGN) {
	sigaction(SIGINT, &new_act, NULL);
    }

    main_pid = getpid();
    memset(&server_info, 0, sa_in_len);
    memset(&cli_info, 0, sa_in_len);

    tcp_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_listen_fd < 0) {
	printf("socket(tcp) fault\n");
	perror(":::");
	sigint_hand(2);
    }
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(port);

    for (i = 0; i < sizeof(server_address) / MAX_IP_LENGTH; i++) {
	if (inet_aton(server_address[i], (struct in_addr *)&server_info.sin_addr) == 0) {
	    printf("inet_aton() fault\n");
	    perror(":::");
	    sigint_hand(2);
	}
	printf("trying bind TCP server to %s:%d - ", server_address[i], ntohs(server_info.sin_port));
	bind_res = bind(tcp_listen_fd, (struct sockaddr *)&server_info, sizeof(server_info));
	if (bind_res < 0) {
	    printf("shit\n");
	    perror(":::");
	} else {
	    printf("OK binded\n");
	    if (listen(tcp_listen_fd, LISTEN_QUEUE) < 0) {
		printf("listen() fault\n");
		perror(":::");
		sigint_hand(2);
	    }

	    //udp_socket
	    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	    if (udp_socket < 0) {
		printf("socket(UDP) fault\n");
		perror(":::");
		sigint_hand(2);
	    }
	    if (bind(udp_socket, (struct sockaddr *)&server_info, sizeof(server_info)) < 0) {
		printf("bind(UDP) fault\n");
		perror(":::");
		sigint_hand(2);
	    }
	    udp_port = ntohs(server_info.sin_port);
	    server_ip = inet_ntoa(server_info.sin_addr); //no error check, cause of checked
	    printf("UDP server started on %s:%d\n", server_ip, udp_port);

	    i = sizeof(server_address) / MAX_IP_LENGTH; //to stop select accessible ip-addresses
	}
    }
    if (bind_res < 0) {
	printf("bind (tcp) fault\n");
	perror(":::");
	sigint_hand(2);
    }

    mq_any_attr.mq_flags = 0;
    mq_any_attr.mq_maxmsg = QUEUE_SIZE;
    mq_any_attr.mq_msgsize = mq_buf_len;
    mq_any_attr.mq_curmsgs = 0;

    tcp_pid = fork();
    start_pid_cer++; //for good SIGINT handling
    if (tcp_pid == -1) {
	start_pid_cer--;
	printf("%c%d%c fork() fault\n", PRINT_PID);
	perror(":::");
	sigint_hand(2);
    }
    if (tcp_pid == 0) {
	int status = 0;
	tcp_pid = getpid();
	tinfo = calloc(pids_num, sizeof(struct thread_info));
	tids = calloc(pids_num, sizeof(pthread_t));

	mq_tcp = mq_open(mq_tcp_name, O_CREAT | O_RDWR, S_IRWXU, &mq_any_attr);
	if (mq_tcp == -1) {
	    printf("%c%d%c mq_open() fault\n", PRINT_PID);
	    perror(":::");
	    sigint_hand(2);
	}
	for (i = 0; i < pids_num; i++) {
	    tinfo[i].tnum = i + 1;
	    dest = &tinfo[i].ser_sa_in;
	    src = &server_info;
	    memcpy(dest, src, sa_in_len);
	    dest = &tinfo[i].mq_tattr;
	    src = &mq_any_attr;
	    memcpy(dest, src, sizeof(struct mq_attr));
	    tinfo[i].tmessage = message;
	    tinfo[i].tmsg_len = sizeof(message);
	    status = pthread_create(&tids[i], NULL, tcp_thread, &tinfo[i]);
	    if (status) {
		printf("%c%d%c pthread_create() fault\n", PRINT_PID);
		perror(":::");
		sigint_hand(2);
	    }
	}
	while (1) {
	    tcp_accept_fd = accept(tcp_listen_fd, (struct sockaddr *)&cli_info,
					(socklen_t *)&sa_in_len);
	    if (tcp_accept_fd < 0) {
		printf("%c%d%c accept(tcp) fault\n", PRINT_PID);
		perror(":::");
		sigint_hand(2);
	    }
	    printf("%c%d%c accepted client(%s:%d)\n",
			PRINT_PID, inet_ntoa(cli_info.sin_addr),
			ntohs(cli_info.sin_port));
	    mq_tcp_msg.fd = tcp_accept_fd;
	    dest = &mq_tcp_msg.cli_sa_in;
	    src = &cli_info;
	    memcpy(dest, src, sa_in_len);
	    res = mq_send(mq_tcp, (char *)&mq_tcp_msg, mq_buf_len, 0);
	    if (res == -1) {
		printf("%c%d%c mq_send(mq_tcp_msg) fault\n", PRINT_PID);
		perror(":::");
		sigint_hand(2);
	    }
	    printf("%c%d%c mq_send(mq_tcp_msg)\n", PRINT_PID);
	}
    }

    mq_udp = mq_open(mq_udp_name, O_CREAT | O_RDWR, S_IRWXU, &mq_any_attr);
    if (mq_udp == -1) {
	printf("%c%d%c mq_open() fault\n", PRINT_PID);
	perror(":::");
	sigint_hand(2);
    }

    for (i = 0; i < pids_num; i++) {
	pid = fork();
	start_pid_cer++; //for good SIGINT handling
	if (pid == -1) {
	    start_pid_cer--;
	    printf("%c%d%c fork() fault\n", PRINT_PID);
	    perror(":::");
	    sigint_hand(2);
	}
	if (pid == 0) {
	    char *cli_ip = NULL;
	    short cli_port = 0;
	    while (1) {
		bytes = mq_receive(mq_udp, (char *)&mq_udp_msg, mq_buf_len, 0);
		if (bytes < mq_buf_len) {
		    printf("%c%d%c mq_receive(%d/%d received) fault\n",
				PRINT_PID, bytes, mq_buf_len);
		    perror(":::");
		    sigint_hand(2);
		}
		cli_ip = inet_ntoa(mq_udp_msg.cli_sa_in.sin_addr);
		cli_port = mq_udp_msg.cli_sa_in.sin_port;
		#if TIME
		sleep(10);
		#endif
		bytes = sendto(mq_udp_msg.fd, message, sizeof(message), 0,
				(struct sockaddr *)&mq_udp_msg.cli_sa_in, sa_in_len);
		if (bytes < sizeof(message)) {
		    printf("%c%d%c sendto(%d/%lu sended) to %s:%d fault\n",
				PRINT_PID, bytes, sizeof(message), cli_ip, cli_port);
		    perror(":::");
		    sigint_hand(2);
		}
		printf("%c%d%c sendto(%d bytes) client (%s:%d)\n",
			    PRINT_PID, bytes, cli_ip, ntohs(cli_port));
	    }
	}

    }

    while (1) {
	bytes = recvfrom(udp_socket, buf_for_recv,
			COND_MSG_SIZE, 0, (struct sockaddr *)&cli_info,
			(socklen_t *)&sa_in_len);
	if (bytes <= 0) {
	    printf("%c%d%c recvfrom() fault\n", PRINT_PID);
	    perror(":::");
	    sigint_hand(2); //if bytes == 0 it is not error, but I end it like error...
	}
	printf("%c%d%c recvfrom(%d bytes) client(%s:%d): %s\n",
			PRINT_PID, bytes, inet_ntoa(cli_info.sin_addr),
			ntohs(cli_info.sin_port), buf_for_recv);
	mq_udp_msg.fd = udp_socket;
	dest = &mq_udp_msg.cli_sa_in;
	src = &cli_info;
	memcpy(dest, src, sa_in_len);
	res = mq_send(mq_udp, (char *)&mq_udp_msg, mq_buf_len, 0);
	if (res == -1) {
	    printf("%c%d%c mq_send(mq_udp_msg) fault\n", PRINT_PID);
	    perror(":::");
	    sigint_hand(2);
	}
	printf("%c%d%c mq_send(mq_udp_msg)\n", PRINT_PID);
    }
}

void sigint_hand (int signal) {
    int i = 0, status;
    pid_t pid = getpid(), wid;
    if (main_pid == pid) {
	printf("\n%c%d%c SIGINT: need wait %d\n", PRINT_PID, start_pid_cer);
	while (start_pid_cer != 0) {
	    wid = wait(&status);
	    if (WIFEXITED(status)) printf("%d terminated normaly\n", wid);
	    if (WIFSIGNALED(status)) {
		printf("%d terminated by sig %d\n", wid, WTERMSIG(status));
		if (WCOREDUMP(status)) printf("%d produced a core dump\n", wid);
	    }
	    if (WIFCONTINUED(status)) printf("%d get SIGCONT\n", wid);
	    start_pid_cer--;
	}
	printf("%c%d%c SIGINT: ended %d\n", PRINT_PID, start_pid_cer);
	if (close(tcp_listen_fd)   == -1) printf("%c%d%c close(listen) for nothing\n", PRINT_PID);
	if (close(tcp_accept_fd)   == -1) printf("%c%d%c close(accept) for nothing\n", PRINT_PID);
	if (close(udp_socket)      == -1) printf("%c%d%c close(udp_sock) for nothing\n", PRINT_PID);
	if (mq_close(mq_udp)       == -1) printf("%c%d%c mq_close(udp) for nothing\n", PRINT_PID);
	if (mq_unlink(mq_udp_name) == -1) printf("%c%d%c mq_unlink(udp) for nothing\n", PRINT_PID);
	printf("%c%d%c end good\n", PRINT_PID);
	exit(0);
    } else {
	if (close(tcp_listen_fd)   == -1) printf("%c%d%c close(listen) for nothing\n", PRINT_PID);
	if (close(tcp_accept_fd)   == -1) printf("%c%d%c close(accept) for nothing\n", PRINT_PID);
	if (close(udp_socket)      == -1) printf("%c%d%c close(udp_sock) for nothing\n", PRINT_PID);
	if (pid == tcp_pid) {
	    for (i = 0; i < pids_num; i++) {
		status = pthread_cancel(tids[i]);
		if (status) {
		    printf("%c%d%c pthread_cancel() fault\n", PRINT_PID);
		    perror(":::");
		}
	    }
	    for (i = 0; i < pids_num; i++) {
		if (pthread_join(tids[i], NULL) != 0) {
		    printf("%c%d%c pthread_join() fault\n", PRINT_PID);
		    perror(":::");
		}
	    }
	    // hmmm...smth can go wrong with free(),
	    // but i think pthread_setcancelstate is good decision
	    free(tinfo);
	    free(tids);
	    if (mq_close(mq_tcp)       == -1) printf("%c%d%c mq_close(tcp) for nothing\n", PRINT_PID);
	    if (mq_unlink(mq_tcp_name) == -1) printf("%c%d%c mq_unlink(tcp) for nothing\n", PRINT_PID);
	    printf("%c%d%c end good\n", PRINT_PID);
	    exit(0);
	} else {
	    if (mq_close(mq_udp)       == -1) printf("%c%d%c mq_close(udp) for nothing\n", PRINT_PID);
	    if (mq_unlink(mq_udp_name) == -1) printf("%c%d%c mq_unlink(udp) for nothing\n", PRINT_PID);
	    printf("%c%d%c end good\n", PRINT_PID);
	    exit(0);
	}
    }
}