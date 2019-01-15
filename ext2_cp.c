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

unsigned char *disk;

int main(int argc, char **argv) {

    //Check for proper usage
    if (argc != 4 || strlen(argv[3]) < 2) {
        fprintf(stderr, "Usage: %s <image file name> <file to copy> <dir path>\n", argv[0]);
        exit(1);
    }

    //Open the image file and map it to 'disk' variable
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    //Check for successful call to mmap
    if (disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    //Open the file to be copied
    FILE * fp;
    fp = fopen(argv[2], "r");
    if (fp == NULL) {
    	printf("File not found\n");
    	exit(ENOENT);
    }

    //Get the file size
    fseek(fp, 0L, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    //Get number of blocks needed to copy file
    int num_blocks_needed = blocks_needed(file_size);

    /* Build the path string from the command line */
    char *dir_path;

    //Case where the user entered only <path>
    if (argv[3][0] != '/'){
       dir_path = malloc(sizeof(char)*strlen(argv[3]) + 2);
       dir_path[0] = '/';
       strncpy(&dir_path[1], argv[3], strlen(argv[3]));
       dir_path[strlen(argv[3]) + 1] = '\0';
    }

    //Case where the user entered /<path>
    else {
      dir_path = malloc(sizeof(char)*strlen(argv[3]) + 1);
      strncpy(dir_path, argv[3], strlen(argv[3]));
      dir_path[strlen(argv[3])] = '\0';
    }

    //Create required data structures for modifying the image
    struct ext2_super_block *sb = (struct ext2_super_block *) ((unsigned char*) disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc*) (disk + EXT2_BLOCK_SIZE * 2);

    //Check if there's enough free memory on the disk to copy the file
    int free_blocks = sb->s_free_blocks_count;
    if (num_blocks_needed > free_blocks){
      fprintf(stderr, "No space on disk\n");
    	exit(ENOSPC);
    }

    //Create required data structures for modifying the image
    int *inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
    int *block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);

    //Variables used to traverse the image map
    int next_inode = next_free(inode_bitmap, sb->s_inodes_count);
    int next_block = next_free(block_bitmap, sb->s_blocks_count);

    //Check if there's enough free inodes on the disk to copy the file
    if (next_inode == -1){
      fprintf(stderr, "Not enough space on disk\n");
		    exit(ENOSPC);
    }

    //Find the path to the directory to contain the new file
    int parent_index = find_inode_index(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);

    //If -1, the path was invalid
    if(parent_index == -1){
      fprintf(stderr, "Invalid path\n");
       exit(ENOENT);
    }

    //If -2, the file already exists at the destination
    else if (parent_index == -2){
      fprintf(stderr, "File exists\n");
      exit(EEXIST);
    }

    //Default behaviour
    else {
      //Update metadata
      update_bitmap(next_inode, gd->bg_inode_bitmap, disk);
      gd->bg_free_inodes_count--;
      sb->s_free_inodes_count--;

      //Create a new inode and initialize it
      struct ext2_inode *new_inode = (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((next_inode - 1) * 128));
      new_inode->i_mode = EXT2_S_IFREG;
      new_inode->i_uid = 0;
      new_inode->i_size = file_size;
      new_inode->i_dtime = 0;
      new_inode->i_gid = 0;
      new_inode->i_links_count = 1;
      new_inode->i_blocks = 2 * (num_blocks_needed);
      new_inode->osd1 = 0;
      new_inode->i_generation = 0;
      new_inode->i_file_acl = 0;
      new_inode->i_dir_acl = 0;
      new_inode->i_faddr = 0;
      new_inode->extra[0] = 0;
      new_inode->extra[1] = 0;
      new_inode->extra[2] = 0;
      memset(new_inode->i_block, 0, 15*sizeof(int));

      //Initialize the indirect memory for files larger than a block
      void *indirect_block;
      unsigned int indirect_arr[256];
      unsigned char *data_block;
      memset(indirect_arr, 0, 256*sizeof(int));
      if(num_blocks_needed <= 12){
        num_blocks_needed++;
      }

      //Set all of the blocks required to hold the file
      for(int i=0; i < num_blocks_needed-1; i++){
        if (i < 12){
          new_inode->i_block[i] = next_block;
          data_block = (void *)(disk + (EXT2_BLOCK_SIZE * new_inode->i_block[i]));
        }
        else if (i == 12){
          new_inode->i_block[i] = next_block;
          indirect_block = (void*)((unsigned char *)(disk + (EXT2_BLOCK_SIZE * new_inode->i_block[i])));
          sb->s_free_blocks_count--;
          gd->bg_free_blocks_count--;
          update_bitmap(next_block, gd->bg_block_bitmap, disk);
          free(block_bitmap);
          block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);
          next_block = next_free(block_bitmap, sb->s_blocks_count);
          indirect_arr[i - 12] = next_block;
          memcpy(indirect_block, (void *) indirect_arr, sizeof(int)*256);
          data_block = (unsigned char *)(disk + (EXT2_BLOCK_SIZE * indirect_arr[i-12]));
        }
        else{
          indirect_arr[i - 12] = next_block;
          memcpy(indirect_block, (void *) indirect_arr, sizeof(int)*256);
          data_block = (unsigned char *)(disk + (EXT2_BLOCK_SIZE * indirect_arr[i-12]));
        }
        //Update the image metadata and free heap memory
        sb->s_free_blocks_count--;
        gd->bg_free_blocks_count--;
        free(block_bitmap);
        update_bitmap(next_block, gd->bg_block_bitmap, disk);
        block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);
        next_block = next_free(block_bitmap, sb->s_blocks_count);
        int num_read = fread(data_block, 1, EXT2_BLOCK_SIZE, fp);
        if (num_read == -1){
      		perror("Error reading file");
      	}
      }

    }
    fclose(fp);
    struct ext2_inode *parent_inode = (struct ext2_inode*)
                    (unsigned char *) (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table) + ((parent_index-1) * 128));
    //Check if parent is a regular file
    if (parent_inode->i_mode & EXT2_S_IFREG){
      fprintf(stderr, "No such file or directory\n");
      exit(ENOENT);
    }
    int d_len = dir_len(dir_path);
    char* dir_name = find_dir_name(dir_path);
    create_file (parent_index-1, gd->bg_inode_table, next_inode-1, d_len, dir_name, disk);

    //Free heap memory
    free(dir_path);
    free(inode_bitmap);
    free(block_bitmap);
}
