#include "ext2.h"
#include "ext2_helper.h"


/*
 * Create a mmaped disk from a disk image.
 */
unsigned char *create_disk(char *disk_img){
     int fd = open(disk_img, O_RDWR);
     if (fd < 0){
         perror("open");
         exit(EXIT_FAILURE);
     }

     unsigned char *disk;
     disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
     if (disk == MAP_FAILED){
         perror("mmap");
         exit(EXIT_FAILURE);
     }
     return disk;
 }

/*
 * Return the directory entry given by path, or NULL if it does not exist inside
 * disk_img.
 * Values for flag: 0 if directory, 1 otherwise.
 */
struct ext2_dir_entry *find(char *disk_img, char *path, int *flag){

    unsigned char *disk = create_disk(disk_img);

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
    unsigned int in_tbl_loc = gd->bg_inode_table;
    struct ext2_inode *root_inode = (struct ext2_inode *)(disk + in_tbl_loc*EXT2_BLOCK_SIZE +
                                        (EXT2_ROOT_INO - 1)*sizeof(struct ext2_inode));
    unsigned int root_block_num = root_inode->i_block[0];

    struct ext2_inode *cur_inode;
    char *name;
    unsigned char name_len;
    unsigned int total_rec_len;
    unsigned int block_num = root_block_num;
    unsigned int inode_num = EXT2_ROOT_INO;
    struct ext2_dir_entry *dir_entry = (struct ext2_dir_entry *)(disk + block_num*EXT2_BLOCK_SIZE);
    *flag = 1;

    char *tempstr = calloc(strlen(path) + 1, sizeof(char));
    strcpy(tempstr, path);
    char *token = strtok(tempstr, "/");
    while (token != NULL){
        // loop inside directory.
        total_rec_len = 0;
        while (total_rec_len < EXT2_BLOCK_SIZE){
            dir_entry = (struct ext2_dir_entry *)(disk + block_num*EXT2_BLOCK_SIZE + total_rec_len);
            inode_num = dir_entry->inode;
            name_len = dir_entry->name_len;
            name = dir_entry->name;
            name[name_len] = '\0';
            if (strcmp(name, token) == 0){
                // move along the path
                break;
            }
            total_rec_len = total_rec_len + dir_entry->rec_len;
            if (total_rec_len >= EXT2_BLOCK_SIZE){
                // there is no such file or directory
                if (path[(strlen(path) - 1)] == '/'){
                    *flag = 0;
                }
                free(tempstr);
                return NULL;
            }
        }

        cur_inode = (struct ext2_inode *)(disk + in_tbl_loc*EXT2_BLOCK_SIZE +
                                (inode_num - 1)*sizeof(struct ext2_inode));
        // every directory entry uses one block (index 0)
        block_num = cur_inode->i_block[0];
        token = strtok(NULL, "/");
    }

    *flag = (dir_entry->file_type == EXT2_FT_DIR) ? 0 : 1;
    free(tempstr);

    return dir_entry;
}

/*
 * Write the file with file name <file_name> inside <disk_img> at directory entry
 * <dir_entry> in a new inode.
 * Return 0 on success, 1 otherwise.
 */
int write_file(char *disk_img, struct ext2_dir_entry *target_dir_entry, char* file_name){

  unsigned char *disk = create_disk(disk_img);
  int fd = open(file_name, O_RDONLY);
  if (fd < 0){
      perror("open");
      exit(EXIT_FAILURE);
  }

  // check if we have free inodes and blocks to write the file.
  struct ext2_super_block *sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
  if (sb->s_free_inodes_count == 0){
      fprintf(stderr, "No available inodes\n");
      return 1;
  }

  // now get the index to first free inode.
  struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2*EXT2_BLOCK_SIZE);
  unsigned int in_bm_loc = gd->bg_inode_bitmap;
  unsigned char *inode_bitmap = (unsigned char *)(disk + in_bm_loc*EXT2_BLOCK_SIZE);
  int inode_num, i, j, k;
  char byte;
  k = -1;
  for (i = 0; i < 4; i++){
      byte = inode_bitmap[i];
      for (j = 0; j < 8; j++){
          if ((byte & (1 << j)) != 0) {
              k++;
          } else {
              inode_num = k;
              break;
          }
      }
      if (k != -1){
          break;
      }
  }

  // get the number of blocks required to write new_file.
  int file_size = lseek(fd, 0, SEEK_END);
  int fd_num_blocks = ((file_size -1) / EXT2_BLOCK_SIZE + 1);
  int indirect_block = 0;
  if (fd_num_blocks > 12){
      indirect_block = 1; // need an indirect block to point to other blocks.
      fd_num_blocks++;
  }

  // check if the free blocks are available for required number of blocks.
  unsigned int free_blocks_count = sb->s_free_blocks_count;
  if (fd_num_blocks > free_blocks_count){
      fprintf(stderr, "Not enough blocks to write the file\n");
      return 1;
  }

  // get the all the free blocks
  unsigned int bl_bm_loc = gd->bg_inode_bitmap;
  unsigned char *block_bitmap = (unsigned char *)(disk + bl_bm_loc*EXT2_BLOCK_SIZE);
  unsigned char *source_file = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  k = 0;
  int copied = 0
  int size_remaining = file_size;
  int block_index;
  for (i = 0; i < 4; i++){
      byte = block_bitmap[i];
      for (j = 0; j < 8 ; j++){
          if ((byte & (1 << j)) != 0){
              k++;
          } else {
              block_index = k;
              set_bit(block_bitmap, block_index, 1);
              if (size_remaining < EXT2_BLOCK_SIZE){
                  memcpy(target_dir_entry + EXT2_BLOCK_SIZE * (block_index + 1),
                                source_file + copied, size_remaining);
                  size_remaining = 0;
              } else {
                  memcpy(target_dir_entry + EXT2_BLOCK_SIZE * (block_index + 1),
                                source_file + copied, EXT2_BLOCK_SIZE);
                  size_remaining = size_remaining - EXT2_BLOCK_SIZE;
                  copied = copied + EXT2_BLOCK_SIZE;
              }
              k++;
          }
      }
  }

  if (copied != file_size){
      fprintf(stderr, "could not write entire file\n");
      return 1;
  }

  set_bit(inode_bitmap, inode_num, 1);

  if (close(fd) < 0){
      perror("close");
      exit(EXIT_FAILURE);
  }

  return 0;
}

/*
 * Change the bit in the given <bitmap> in positions <pos> with <new_bit>.
 */
void set_bit(unsigned char *bitmap, int pos, int new_bit){
  int index = pos / 8;
  int bit = pos % 8;
  unsigned char *new_byte = bitmap + index;
  *new_byte = (*new_byte & ~(1 << bit)) | (new_bit << bit);
}
