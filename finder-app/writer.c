#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


int main(int argc, char** argv){
    if (argc != 3){
        syslog(LOG_ERR, "This program requires 2 arguments\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "w+");
    syslog(LOG_USER,"Writing %s to %s\n", argv[2], argv[1]);
    fwrite(argv[2], strlen(argv[2]), 1, f);

    return 0;
}