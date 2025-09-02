#include <unistd.h>

int
main(void) {
    int p[2];
    char *argv[2];
    argv[0] = "wc";
    argv[1] = NULL;
    pipe(p);

    if (fork() == 0) {
        close(STDIN_FILENO);
        dup(p[0]);
        close(p[0]);
        // close(p[1]);
        execv("/bin/wc", argv);
    } else {
        close(p[0]);
        write(p[1], "hello world\n", 12);        
        close(p[1]);
    }
    return 0;
}