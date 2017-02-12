#include "ext2_helper.c"

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_restore <image file name> <absolute path>\n");
        exit(1);
    }

    return 0;
}
