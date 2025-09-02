#include "kernel/types.h"
#include "user.h"

int
main(void) {
    int pid;
    int p[2];
    if (pipe(p) < 0) {
        printf("pingpong: pipe failed\n");
        exit(-1);
    }
    if ((pid = fork()) < 0) {
        printf("pingpong: fork failed\n");
        exit(-1);
    } else if (pid == 0) {
        // child
        int parent_id = -1;
        int child_id = getpid();
        read(p[0], &parent_id, sizeof(int));
        printf("%d: received ping from pid %d\n", child_id, parent_id);
        write(p[1], &child_id, sizeof(int));
        close(p[0]);
        close(p[1]);
        exit(0);
    } else {
        // parent
        int parent_id = getpid();
        int child_id = -1;
        write(p[1], &parent_id, sizeof(int));

        // key: cause a yield
        sleep(1);
        
        read(p[0], &child_id, sizeof(int));
        printf("%d: received pong from pid %d\n", parent_id, child_id);
        int exit_code;
        wait(&exit_code);
        close(p[0]);
        close(p[1]);
    }
    exit(0);
}