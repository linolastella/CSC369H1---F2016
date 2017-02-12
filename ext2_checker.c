#include "ext2_helper.c"


int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: ext2_checker <image file name>\n");
        exit(EXIT_FAILURE);
    }
    char *disk_img = argv[1];
    unsigned char *disk = create_disk(disk_img);

    int N = 0; // number of inconsistencies.
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
    unsigned int bl_bm_loc = gd->bg_block_bitmap;
    unsigned int in_bm_loc = gd->bg_inode_bitmap;
    unsigned int in_tbl_loc = gd->bg_inode_table;
    unsigned int used_dirs = gd->bg_used_dirs_count;
    int sb_counter = sb->s_free_blocks_count;
    int gd_counter = gd->bg_free_blocks_count;

    // part a)
    // free blocks check.
    int i, j, k, diff, real_counter;
    char byte;
    unsigned char *block_bitmap = (unsigned char *)(disk + bl_bm_loc*EXT2_BLOCK_SIZE);
    int nice_block_bitmap[sb->s_blocks_count];
    real_counter = 0;
    k = 0;
    for (i = 0; i < 16; i++){
        byte = block_bitmap[i];
        for (j = 0; j < 8 ; j++){
            if ((byte & (1 << j)) != 0) { // bit is off
                nice_block_bitmap[k] = 1;
                k++;
            } else {
                real_counter++;
                nice_block_bitmap[k] = 1;
                k++;
            }
        }
    }
    if (sb_counter != real_counter){
        diff = abs(sb_counter - real_counter);
        sb->s_free_blocks_count = real_counter;
        printf("Fixed: superblock's free blocks counter was off by %d compared "
                "to the bitmap\n", diff);
        N = N + diff;
    }
    if (gd_counter != real_counter){
        diff = abs(gd_counter - real_counter);
        gd->bg_free_blocks_count = real_counter;
        printf("Fixed: block group's free blocks counter was off by %d compared "
                "to the bitmap\n", diff);
        N = N + diff;
    }

    // free inodes check.
    unsigned char *inode_bitmap = (unsigned char *)(disk + in_bm_loc*EXT2_BLOCK_SIZE);
    int nice_inode_bitmap[sb->s_inodes_count];
    real_counter = 0;
    k = 0;
    for (i = 0; i < 4; i++){
        byte = inode_bitmap[i];
        for (j = 0; j < 8; j++){
            if ((byte & (1 << j)) != 0) { // bit is on
                nice_inode_bitmap[k] = 1;
                k++;
            } else {
                real_counter++;
                nice_inode_bitmap[k] = 0;
                k++;
            }
        }
    }
    sb_counter = sb->s_free_inodes_count;
    if (sb_counter != real_counter){
        diff = abs(sb_counter - real_counter);
        sb->s_free_inodes_count = real_counter;
        printf("Fixed: superblock's free inodes counter was off by %d compared "
                "to the bitmap\n", diff);
        N = N + diff;
    }
    gd_counter = gd->bg_free_inodes_count;
    if (gd_counter != real_counter){
        diff = abs(gd_counter - real_counter);
        gd->bg_free_inodes_count = real_counter;
        printf("Fixed: block group's free inodes counter was off by %d compared "
                "to the bitmap\n", diff);
        N = N + diff;
    }

    // loop through each directory entry
    struct ext2_dir_entry *dir_entry;
    struct ext2_inode *inode;
    unsigned int inode_num, total_rec_len, dbc, block_nums[used_dirs - 1];
    char *name;

    // initialize dir_entry and inode as root.
    inode = (struct ext2_inode *)(disk + in_tbl_loc*EXT2_BLOCK_SIZE +
                            (EXT2_ROOT_INO - 1)*sizeof(struct ext2_inode));
    dir_entry = (struct ext2_dir_entry *)(disk + (inode->i_block[0])*EXT2_BLOCK_SIZE);

    // j loops through the blocks of the directory (usually only one)
    // whereas dbc (data block counter) is a global counter for the
    // array of block numbers.
    dbc = 0;
    j = 0;
    while (inode->i_block[j] != 0) {
        block_nums[dbc] = inode->i_block[j];
        dbc++;
        j++;
    }

    for (i = 0; i < used_dirs - 1; i++){
        total_rec_len = 0;
        while (total_rec_len < EXT2_BLOCK_SIZE) {
            dir_entry = (struct ext2_dir_entry *)(disk + block_nums[i]*EXT2_BLOCK_SIZE + total_rec_len);
            inode_num = dir_entry->inode;
            if (inode_num == EXT2_ROOT_INO || inode_num > EXT2_GOOD_OLD_FIRST_INO){
                inode = (struct ext2_inode *)(disk + in_tbl_loc*EXT2_BLOCK_SIZE +
                                        (inode_num-1)*sizeof(struct ext2_inode));

                // check i_mode.
                if ((inode->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR){
                    j = 0;
                    while (inode->i_block[j] != 0){
                        name = dir_entry->name;
                        name[dir_entry->name_len] = '\0';
                        if (strcmp(name, "..") != 0 && strcmp(name, ".") != 0){
                            block_nums[dbc] = inode->i_block[j];
                            dbc++;
                        }
                        j++;
                    }
                    if (dir_entry->file_type != EXT2_FT_DIR){
                        dir_entry->file_type = EXT2_FT_DIR;
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_num);
                        N++;
                    }
                } else if ((inode->i_mode & EXT2_S_IFREG) == EXT2_S_IFREG){
                    if (dir_entry->file_type != EXT2_FT_REG_FILE){
                        dir_entry->file_type = EXT2_FT_REG_FILE;
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_num);
                        N++;
                    }
                } else if ((inode->i_mode & EXT2_S_IFLNK) == EXT2_S_IFLNK){
                    if (dir_entry->file_type != EXT2_FT_SYMLINK){
                        dir_entry->file_type = EXT2_FT_SYMLINK;
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_num);
                        N++;
                    }
                }

                // check inode bitmap
                if (nice_inode_bitmap[inode_num - 1] == 0){
                    // we need to set this bitmap and update superblock and
                    // group descriptor
                    sb->s_free_inodes_count--;
                    gd->bg_free_inodes_count--;
                    set_bit(inode_bitmap, inode_num, 1);
                    printf("Fixed: inode [%d] not marked as in-use\n", inode_num);
                    N++;
                }

                // check i_dtime
                if (inode->i_dtime != 0){
                    inode->i_dtime = 0;
                    printf("Fixed: valid inode marked for deletion: [%d]\n", inode_num);
                    N++;
                }

                // check block bitmap
                
            }

            total_rec_len = total_rec_len + dir_entry->rec_len;
        }
    }

    // print last message
    if (N == 0){
        printf("No file system inconsistencies detected!\n");
    } else {
        printf("%d file system inconsistencies repaired!\n", N);
    }

    return 0;
}
