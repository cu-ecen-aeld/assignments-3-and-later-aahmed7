#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT "9000"
#define BUF_SIZE 1024
#define NUM_CLIENTS 10

#define USE_AESD_CHAR_DEVICE 1

#if (USE_AESD_CHAR_DEVICE)
    #define OUTFILE "/dev/aesdchar"
#else
    #define OUTFILE "/var/tmp/aesdsocketdata"
#endif

#define AESDCHAR_IOCSEEKTO_CMD "AESDCHAR_IOCSEEKTO:"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_data{
    int client_fd;
    socklen_t peer_addrlen;
    struct sockaddr_storage peer_addr;
};

struct node
{
    pthread_t thread;
    // This macro does the magic to point to other nodes
    TAILQ_ENTRY(node) nodes;
};

volatile sig_atomic_t done = 0;

void sigterm_handler(int s){
    printf("Caught signal, exiting\n");
    syslog(LOG_USER,"Caught signal, exiting\n");
    done = 1;
}

void sigint_handler(int s){
    printf("Caught signal, exiting\n");
    syslog(LOG_USER,"Caught signal, exiting\n");
    done = 1;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *ts_thread_func(void* thread_param){
    while (done==0) {
        char outstr[200];
        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = localtime(&t);
        if (tmp == NULL) {
            perror("localtime");
            break;
        }

        if (strftime(outstr, sizeof(outstr), "timestamp:%a, %d %b %Y %T %z", tmp) == 0) {
            fprintf(stderr, "strftime returned 0");
            break;
        }
        printf("Result string is \"%s\"\n", outstr);

        pthread_mutex_lock(&mutex);
        FILE *fp = fopen(OUTFILE, "a");
        if (fp != NULL){
            // fwrite(&outstr, 1, sizeof(outstr), fp);
            fprintf(fp, "%s\n", outstr);
            fclose(fp);
        }
        pthread_mutex_unlock(&mutex);

        sleep(10);
    }
    return thread_param;
}

void *conn_thread_func(void* thread_param) {
    ssize_t nread = 0;
    char buf[BUF_SIZE] = {0};
    char s[INET6_ADDRSTRLEN] = {0};
    struct thread_data *tdata = (struct thread_data *)thread_param;
    struct aesd_seekto seekto = {0};

    // get peer address.
    inet_ntop(tdata->peer_addr.ss_family, get_in_addr((struct sockaddr*)&tdata->peer_addr), s, sizeof(s));
    syslog(LOG_USER, "Accepted connection from %s\n", s);

    if (pthread_mutex_lock(&mutex) != 0){
        perror("pthread_mutex_lock");
        close(tdata->client_fd);
        syslog(LOG_USER, "Closed connection from %s\n", s);
        return thread_param;
    }

    int fd1 = open(OUTFILE, O_WRONLY | O_APPEND | O_CREAT, 0666);;
    if (fd1 == -1){
        perror("open");
        syslog(LOG_ERR, "open1 failed");
        exit(-1);
    }
    int fd2 = open(OUTFILE, O_RDONLY);
    if (fd2 == -1){
        perror("fopen");
        exit(-1);
    }

    while((nread = recv(tdata->client_fd, &buf, BUF_SIZE, 0)) > 0) {
        syslog(LOG_USER, "socket received: %s", buf);
        int ret = sscanf(buf, "AESDCHAR_IOCSEEKTO:%u,%u\n", &seekto.write_cmd, &seekto.write_cmd_offset);
        if (ret == 2){
            syslog(LOG_USER, "token 1: %d, token 2: %d\n", seekto.write_cmd, seekto.write_cmd_offset);
            if (ioctl(fd2, AESDCHAR_IOCSEEKTO, &seekto) != 0) {
                syslog(LOG_ERR, "ioctl AESDCHAR_IOCSEEKTO failed: %s", strerror(errno));
            }
        } else {
            if(write(fd1, &buf, nread) == -1) {
                perror("write");
                syslog(LOG_ERR, "write failed");
                close(fd1);
                return thread_param;
            }
        }
        for (int i = 0; i < nread; i++){
            if (buf[i] == '\n') {
                syslog(LOG_USER,"got newline\n");
                break;
            }
        }
    }
    close(fd1);

    while((nread = read(fd2, buf, BUF_SIZE)) > 0) {
        ssize_t nwrite = 0;
        if ((nwrite = send(tdata->client_fd, buf, nread, 0)) == -1)
        {
            perror("send");
            break;
        }
    }
    close(fd2);

    if (pthread_mutex_unlock(&mutex) != 0){
        perror("pthread_mutex_unlock");
    }

    close(tdata->client_fd);
    syslog(LOG_USER, "Closed connection from %s\n", s);

    return thread_param;
}

int main(int argc, char *argv[]){
    if (argc > 1){
        // if we have some extra arguments.
        if (strncmp(argv[1], "-d", strlen("-d")) == 0){
            // if daemonmode was specified.
            pid_t child_pid = fork();
            if (child_pid == -1) { perror("fork"); exit(-1);}
            if (child_pid != 0) {
                // exit parent
                exit(0);
            }
        }else{
            fprintf(stderr,"Some invalid arguments were passed and ignored\n");
        }
    }
    int sfd, yes = 1;
    struct addrinfo hints, *result, *rp;
    int rv;
    struct node * e = NULL;
    pthread_t ts_thread = {0};

    struct sigaction sa_sigterm;
    memset(&sa_sigterm, 0, sizeof(sa_sigterm));
    sa_sigterm.sa_handler = sigterm_handler;
    sigemptyset(&sa_sigterm.sa_mask);
    if (sigaction( SIGTERM, &sa_sigterm, NULL) == -1){ //
        // perror("sigaction");
        exit(-1);
    }

    struct sigaction sa_sigint;
    memset(&sa_sigint, 0, sizeof(sa_sigint));
    sa_sigint.sa_handler = sigint_handler;
    sigemptyset(&sa_sigint.sa_mask);
    if (sigaction( SIGINT, &sa_sigint, NULL) == -1){ // 
        // perror("sigaction");
        exit(-1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    rv = getaddrinfo(NULL, PORT, &hints, &result);
    if (rv != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        syslog(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(-1);
    }

    for (rp = result; rp != NULL; rp = rp -> ai_next){
        sfd = socket (rp->ai_family, rp -> ai_socktype, rp -> ai_protocol);
        if (sfd == -1) {
            // perror("server: socket");
            syslog(LOG_ERR, "failed to create socket\n");
            continue;
        }

        if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
            close(sfd);
            // perror("setsockopt");
            syslog(LOG_ERR, "failed to set socket options\n");
            exit (-1);
        }

        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == -1) {
            close(sfd);
            // perror("server: bind");
            syslog(LOG_ERR, "failed to bind socket\n");
            continue;
        }
        break;
    }
    freeaddrinfo(result);

    if (rp == NULL){
        syslog(LOG_ERR, "failed to bind\n");
        exit(-1);
    }

    // Start listening for a connection.
    if (listen(sfd, NUM_CLIENTS) == -1){
        perror("listen");
        syslog(LOG_ERR, "failed to open socket\n");
        exit(-1);
    }

    TAILQ_HEAD(head_s, node) head;
    TAILQ_INIT(&head);

#if !(USE_AESD_CHAR_DEVICE)
    if(pthread_create(&ts_thread, NULL, ts_thread_func, NULL) != 0){
        perror("ts_thread create");
        done = 1;
        goto cleanup;
    }
#endif

    syslog(LOG_USER, "waiting for connections...\n");
    while(done == 0){
        pthread_t thread = {0};
        struct thread_data *tdata;
        int s = 0;
        tdata = calloc(1, sizeof(struct thread_data));
        if (tdata == NULL){
            perror("calloc");
            continue;
        }
        tdata->peer_addrlen = sizeof(tdata->peer_addr);

        // Wait for a connection.
        tdata->client_fd = accept(sfd, (struct sockaddr*)&tdata->peer_addr, &tdata->peer_addrlen);
        if (tdata->client_fd == -1){
            // perror("accept");
            syslog(LOG_ERR, "failed to accept connection socket\n");
            continue;
        }
        s = pthread_create(&thread, NULL, conn_thread_func, tdata);
        if (s != 0){
            printf("Failed to create thread\n");
            syslog(LOG_USER,"Failed to create thread\n");
            perror("pthread_create");
            free(tdata);
        }

        e = malloc(sizeof(struct node));
        if (e == NULL)
        {
            printf("Failed to create list node\n");
            syslog(LOG_USER,"Failed to create list node\n");
            perror("malloc failed");
            break;
        }
        e->thread = thread;
        TAILQ_INSERT_TAIL(&head, e, nodes);
        e=NULL;
    }

    while (!TAILQ_EMPTY(&head))
    {
        e = TAILQ_FIRST(&head);
        pthread_join(e->thread, NULL);
        TAILQ_REMOVE(&head, e, nodes);
        free(e);
        e = NULL;
    }

    // Cleanup.
cleanup:
#if !(USE_AESD_CHAR_DEVICE)
    remove(OUTFILE);
#endif
    pthread_mutex_destroy(&mutex);
    close(sfd);

    return 0;
}