#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "math.h"
#include <errno.h>
#include "string.h"
#include "ext2_functions.h"
#include <time.h>


unsigned char *disk;

int main(int argc, char **argv) {

    //Check for proper usage
    if(argc != 3 || strlen(argv[2]) < 2 || argv[2][0] != '/') {
        fprintf(stderr, "Usage: %s <image file name> <absolute dir path>\n", argv[0]);
        exit(1);
    }

    //Open the image file
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    //Build the file path in memory
    time_t seconds;
    char *dir_path;
    dir_path = malloc(sizeof(char)*strlen(argv[2]) + 1);
    strncpy(dir_path, argv[2], strlen(argv[2]));
    dir_path[strlen(argv[2])] = '\0';

    //Create the superblock and group description
    struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
    char* file_name = find_dir_name(dir_path);

    //Get the inode and file type of the file to remove
    int index = inode_index(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);
    int ftype = file_type(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);

    //Check if its a directory
    if (ftype == EXT2_FT_DIR){
      fprintf(stderr, "Cannot remove directory\n");
      return(-EISDIR);
    }

    //Check if it exists
    else if(index == -1){
      fprintf(stderr, "No such file.\n");
      return(-ENOENT);
    }
    /*index = 10;
    printf("index = %d\n", index);
    int *block_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
    for(int i =0; i < sb->s_inodes_count; i++){
      if(i > 0 && i%8 == 0){
        printf(" ");
      }
      printf("%d", block_bitmap[i]);
    }
    printf("\n");
    rm_update_bitmap(index, gd->bg_inode_bitmap, disk);
    free(block_bitmap);
    block_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
    for(int i =0; i < sb->s_inodes_count; i++){
      if(i > 0 && i%8 == 0){
        printf(" ");
      }
      printf("%d", block_bitmap[i]);
    }
    printf("\n");*/


    //Get the path to the parent directory of the file to remove
    char * parent_path = malloc(sizeof(char)*(strlen(dir_path) - strlen(file_name) + 1));
    strncpy(parent_path, dir_path, strlen(dir_path) - strlen(file_name));
    parent_path[strlen(dir_path) - strlen(file_name)] = '\0';
    int parent_index;
    //Check for path is the root
    if(parent_path[0] == '/' && strlen(parent_path) == 1){
      parent_index = 2;
    }
    else{
      parent_index = inode_index(parent_path, sb->s_inodes_count, gd->bg_inode_table, disk);
    }

    int j = 0;
    struct ext2_inode *curr_inode =
            (struct ext2_inode *) ((unsigned char *) disk
                        + (1024*gd->bg_inode_table) + ((parent_index-1) * 128));
    struct ext2_inode *remove_inode =
            (struct ext2_inode *) ((unsigned char *) disk
                        + (1024*gd->bg_inode_table) + ((index-1) * 128));
    seconds = time(NULL);
    remove_inode->i_dtime = seconds;
    remove_inode->i_links_count = 0;

    for(int i=0; i < EXT2_BLOCK_SIZE;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0])+i);
      struct ext2_dir_entry *next_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0])+i+curr_dir->rec_len);

      //If the file has been found, remove it
      if(strncmp(next_dir->name, file_name, strlen(file_name)) == 0){
        curr_dir->rec_len += next_dir->rec_len;
	//Update the image metadata
        rm_update_bitmap(index, gd->bg_inode_bitmap, disk);
      	sb->s_free_inodes_count++;
      	gd->bg_free_inodes_count++;
        //Remove the first 12 blocks of memory
        while(remove_inode->i_block[j] != 0 && j < 12
          && remove_inode->i_block[j] < sb->s_blocks_count){
          rm_update_bitmap(remove_inode->i_block[j], gd->bg_block_bitmap, disk);
          sb->s_free_blocks_count++;
          gd->bg_free_blocks_count++;
          j++;
        }

        //Remove the file from indirect blocks of memory
        j = 0;
        if(remove_inode->i_block[12] != 0 && remove_inode->i_block[j] < sb->s_blocks_count){
          for(; j < EXT2_BLOCK_SIZE; j++){
            int * indirect_block = (int*)((unsigned char *)(disk + (EXT2_BLOCK_SIZE * remove_inode->i_block[12])));
            if(indirect_block[j] > 0 && (int)indirect_block[j] < sb->s_blocks_count){
              rm_update_bitmap((int)indirect_block[j], gd->bg_block_bitmap, disk);
              sb->s_free_blocks_count++;
              gd->bg_free_blocks_count++;
            }
          }
          //Update the image metadata
          rm_update_bitmap(remove_inode->i_block[12], gd->bg_block_bitmap, disk);
          sb->s_free_blocks_count++;
          gd->bg_free_blocks_count++;
        }
        i=EXT2_BLOCK_SIZE;
      }
      i+=curr_dir->rec_len;
    }
    //Free used memory
    free(dir_path);
    free(parent_path);
}
