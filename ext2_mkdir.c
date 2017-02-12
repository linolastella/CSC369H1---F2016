#include "ext2_helper.c"


int main(int argc, char **argv) {

    if (argc != 3){
        fprintf(stderr, "Usage: ext2_mkdir <image file name> <absolute path>\n");
        exit(EXIT_FAILURE);
    }

    char *disk_img = argv[1];
    char *complete_path = argv[2];
    int *flag = malloc(sizeof(int));
    char *dir_name;
    if (strrchr(complete_path, '/') == NULL){
        dir_name = complete_path;
    } else {
        dir_name = strrchr(complete_path, '/') + 1;
    }
    
    if (find(disk_img, complete_path, flag) != NULL){
        // there already exist a path to this directory.
        free(flag);
        return -EEXIST;
    }

    char *target_path = calloc(strlen(complete_path) + 1, sizeof(char));
    strncpy(target_path, complete_path, strlen(complete_path) - strlen(dir_name));

    struct ext2_dir_entry *dir_entry = find(disk_img, target_path, flag);
    if (dir_entry == NULL){
        // the target path to the new directory does not exist.
        free(flag);
        free(target_path);
        return -ENOENT;
    }

    return 0;
}
