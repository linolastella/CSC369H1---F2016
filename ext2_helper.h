/*
 * helper.c contains all the functions and libraries in common among our
 * multiple C files, together with shared variables.
 */

#ifndef A4_HELPER_H
#define A4_HELPER_H

/* all the libraries are included here */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

/* function declarations */
struct ext2_dir_entry *find(char *disk_img, char *path, int *flag);
unsigned char *create_disk(char *disk_img);
void set_bit(unsigned char *bitmap, int pos, int new_bit);

#endif
