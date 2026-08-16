#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

#ifndef HOSTNAMEMAX
#define HOSTNAMEMAX 4096
#endif

int main(){
    char home_directory[MAXPATHLEN];
    getcwd(home_directory, sizeof(home_directory));
    int home_directory_size = strlen(home_directory);

    while(1){
        char* username = getenv("USER");
        if(username == NULL) username = "no_name_user";

        char hostname[HOSTNAMEMAX];
        gethostname(hostname, sizeof(hostname));

        char cwd[MAXPATHLEN];
        getcwd(cwd, sizeof(cwd));

        if(strncmp(cwd, home_directory, home_directory_size) == 0){
            printf("<%s@%s:~%s> ", username, hostname,  cwd + home_directory_size);
        }
        else{
            printf("<%s@%s:%s> ", username, hostname, cwd);
        }
    }



}

