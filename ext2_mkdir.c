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
    if (argc != 3 || strlen(argv[2]) < 2) {
      fprintf(stderr, "Usage: %s <image file name> <dir path>\n", argv[0]);
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

    /* Build the path string from the command line */
    char *dir_path;
    //Case where the user entered only <path>
    if (argv[2][0] != '/'){
      dir_path = malloc(sizeof(char)*strlen(argv[2]) + 2);
      dir_path[0] = '/';
      strncpy(&dir_path[1], argv[2], strlen(argv[2]));
      dir_path[strlen(argv[2]) + 1] = '\0';
    }

    //Case where the user entered /<path>
    else {
      dir_path = malloc(sizeof(char)*strlen(argv[2]) + 1);
      strncpy(dir_path, argv[2], strlen(argv[2]));
      dir_path[strlen(argv[2])] = '\0';
    }

    //Create required data structures for modifying the image
    struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
    int *inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
    int *block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);

    //Variables used to traverse the image map
    int d_len = dir_len(dir_path);
    char* dir_name = find_dir_name(dir_path);
    int next_inode = next_free(inode_bitmap, sb->s_inodes_count);
    int next_block = next_free(block_bitmap, sb->s_blocks_count);

    //Update bitmaps after setup
    update_bitmap(next_inode, gd->bg_inode_bitmap, disk);
    update_bitmap(next_block, gd->bg_block_bitmap, disk);

    //Get the parent index of the directory to create
    int parent_index = find_inode_index(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);

    //If find_inode_index returns -1, the path was invalid
    if (parent_index == -1){
      printf("Invalid path\n");
      exit(ENOENT);
    }
    //If find_inode_index returns -2, the directory already exists
    else if (parent_index == -2){
      printf("Directory exists\n");
      exit(EEXIST);
    }
    //Default behavior
    else{
      //Create a new inode for the directory and set its initial values
      struct ext2_inode *new_inode = (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((next_inode - 1) * 128));
      new_inode->i_mode = EXT2_S_IFDIR;
      new_inode->i_uid = 0;
      new_inode->i_size = 1024;
      new_inode->i_dtime = 0;
      new_inode->i_gid = 0;
      new_inode->i_links_count = 2;
      new_inode->i_blocks = 2;
      new_inode->osd1 = 0;
      new_inode->i_generation = 0;
      new_inode->i_file_acl = 0;
      new_inode->i_dir_acl = 0;
      new_inode->i_faddr = 0;
      new_inode->extra[0] = 0;
      new_inode->extra[1] = 0;
      new_inode->extra[2] = 0;
      new_inode->i_block[0] = next_block;
      create_dir (parent_index-1, gd->bg_inode_table, next_inode-1, d_len, dir_name, disk);
      
      //Update the metadata
      sb->s_free_blocks_count--;
      sb->s_free_inodes_count--;
      gd->bg_free_blocks_count--;
      gd->bg_free_inodes_count--;
      gd->bg_used_dirs_count++;
    }

    /*free(inode_bitmap);
    inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap);
    for(int i =0; i < sb->s_inodes_count; i++){
      if(i > 0 && i%8 == 0){
        printf(" ");
      }
      printf("%d", inode_bitmap[i]);
    }
    printf("\n");*/

    //Free heap memory
    free(dir_name);
    free(inode_bitmap);
    free(block_bitmap);
    free(dir_path);
}
