#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>

#define PORT "9000"
#define BUF_SIZE 1024
#define OUTFILE "/var/tmp/aesdsocketdata"

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
    int sfd, new_fd, yes = 1;
    struct addrinfo hints, *result, *rp;
    ssize_t nread;
    socklen_t peer_addrlen;
    struct sockaddr_storage peer_addr;
    char s[INET6_ADDRSTRLEN];
    int rv;

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
    if (listen(sfd, 10) == -1){
        // perror("listen");
        syslog(LOG_ERR, "failed to open socket\n");
        exit(-1);
    }
    
    // printf("server: waiting for connections...\n");
    syslog(LOG_USER, "waiting for connections...\n");
    while(done == 0){
        peer_addrlen = sizeof(peer_addr);
        char buf[BUF_SIZE] = {0};

        // Wait for a connection.
        new_fd = accept(sfd, (struct sockaddr*)&peer_addr, &peer_addrlen);
        if (new_fd == -1){
            // perror("accept");
            syslog(LOG_ERR, "failed to accept connection socket\n");
            continue;
        }

        // get peer address.
        inet_ntop(peer_addr.ss_family, get_in_addr((struct sockaddr*)&peer_addr), s, sizeof(s));
        // printf("Accepted connection from %s\n", s);
        syslog(LOG_USER, "Accepted connection from %s\n", s);

        FILE *fp = fopen(OUTFILE, "a+b");
        if (fp == NULL){
            perror("fopen");
            exit(-1);
        }
        do{
            nread = recv(new_fd, &buf, BUF_SIZE, 0);
            if (nread == -1) {
                close(sfd);
                close (new_fd);
                perror("recv");
                exit(-1);
            }else{
                fwrite(&buf, 1, nread, fp);
                fseek(fp, 0, SEEK_SET);
                for (int i = 0; i < nread; i++){
                    if (buf[i] == '\n') {
                        printf("got newline\n");
                        char sendbuf[BUF_SIZE] = {0};
                        int j = fread(sendbuf, sizeof(char), BUF_SIZE, fp);
                        while(j>0) {
                            printf("%d\n", j);
                            if (send(new_fd, sendbuf, j, 0) == -1)
                            {
                                perror("send");
                                break;
                            }
                            j = fread(sendbuf, sizeof(char), BUF_SIZE, fp);
                        }
                    }
                }
            }
        }while(nread != 0);
        fclose(fp);

        close(new_fd);
        syslog(LOG_USER, "Closed connection from %s\n", s);
    }

    // Cleanup.
    remove(OUTFILE);
    close(sfd);

    return 0;
}