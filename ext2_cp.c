#include "ext2_helper.c"


int main(int argc, char **argv) {

    if (argc != 4){
        fprintf(stderr, "Usage: ext2_cp <image file name> <path to file> <absolute path on disk>\n");
        exit(EXIT_FAILURE);
    }

    char *disk_img = argv[1];
    char *native_path = argv[2];
    char *disk_path = argv[3];

    int *flag = malloc(sizeof(int));
    struct ext2_dir_entry *target_dir_entry = find(disk_img, disk_path, flag);
    if (*flag == 0){
        // disk_path is a path to directory, so it must exist.
        if (target_dir_entry == NULL){
            free(flag);
            return -ENOENT;
        } else {
            // copy the file inside this directory entry.

          if(write_file(disk_img, target_dir_entry, native_path) == 1){
            fprintf(stderr, "failed to write\n");
            return 1;
          }
        }

    } else {
        // path is a path to file.
        if (target_dir_entry == NULL){
            // create a file with name as specified in the path.
            char *name;
            if (strrchr(disk_path, '/') == NULL){
                name = disk_path;
            } else {
                name = strrchr(disk_path, '/') + 1;
            }

            printf("%s\n", name);
        } else {
            // this file already exists.
            free(flag);
            return -EEXIST;
        }
    }

    free(flag);

    return 0;
}
