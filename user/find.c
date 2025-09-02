#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user.h"

static char cur_path[512];
static int top;

char*
bare_name(char* full_path) {
    char* p = full_path + strlen(full_path);
    while (p > full_path && *p != '/') p--;
    return p + 1;
}

void
push_dir(char* new_dir) {
    // new_dir should be produced by bare_name()
    cur_path[top++] = '/';
    strcpy(cur_path + top, new_dir);
    top += strlen(new_dir);
    cur_path[top] = 0;
}

void
pop_dir() {
    if (top > 0) {
        top--;
        while (top > 0 && cur_path[top] != '/') top--;
        cur_path[top] = 0;
    } else {
        printf("error");
        exit(-1);
    }
}


void
find(char* name) {
    int fd = open(cur_path, 0);
    if (fd < 0) {
        printf("find: failed to open %s\n", cur_path);
        exit(-1);
    }

    struct dirent de;
    struct stat st;
    char buf[512];
    int idx = strlen(cur_path);
    strcpy(buf, cur_path);
    buf[idx++] = '/';
    
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (strcmp(de.name, name) == 0) {
            printf("%s/%s\n", cur_path, de.name);
        }
        if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
            continue;
        }
        strcpy(buf + idx, de.name);
        buf[idx + strlen(de.name)] = 0;
        if (stat(buf, &st) < 0) {
            printf("find: failed to stat %s\n", buf);
            continue;
        }
        if (st.type == T_DIR) {
            push_dir(de.name);
            find(name);
            pop_dir();
        } else {
            // do nothing
            ;
        }
    }

    close(fd);
}

int
main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("usage: find <path> <name>\n");
        exit(-1);
    }

    for (; *argv[1]; argv[1]++) {
        cur_path[top++] = *argv[1];
    }

    find(argv[2]);

    exit(0);
}