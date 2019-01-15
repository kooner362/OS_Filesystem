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
    if(argc != 3 || strlen(argv[2]) < 2 || argv[2][0] != '/') {
        fprintf(stderr, "Usage: %s <image file name> <dir path>\n", argv[0]);
        exit(1);
    }

    //Open the image file
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    //Check for unsuccessful mmap call
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    //Put the directory into heap memory
    char *dir_path;
    dir_path = malloc(sizeof(char)*strlen(argv[2]) + 1);
    strncpy(dir_path, argv[2], strlen(argv[2]));
    dir_path[strlen(argv[2])] = '\0';

    //Create the superblock and group block
    struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);

    //Check for the file already existing
    char* file_name = find_dir_name(dir_path);
    int index = inode_index(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);
    if(index != -1){
      fprintf(stderr, "File already exists.\n");
      return(-ENOENT);
    }

    //Set up the recovery path
    char *recover_path;
    recover_path = malloc(sizeof(char)*(strlen(dir_path) - strlen(file_name) + 1));
    strncpy(recover_path, dir_path, strlen(dir_path) - strlen(file_name));
    recover_path[strlen(dir_path) - strlen(file_name)] = '\0';

    //Check for the recovery path being root
    int recover_index;
    if (recover_path[0] == '/' && strlen(recover_path) == 1){
      recover_index = 2;
    }
    //Find the inode index for the file to be restored
    else{
      recover_index = inode_index(recover_path, sb->s_inodes_count, gd->bg_inode_table, disk);
    }

    //Create an inode to use for the restored file
    struct ext2_inode *restore_inode =
            (struct ext2_inode *) ((unsigned char *) disk
                        + (1024*gd->bg_inode_table) + ((recover_index-1) * 128));

    //Find the directory for the restored file
    for(int i=0; i < EXT2_BLOCK_SIZE;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*restore_inode->i_block[0])+i);
      int orig_rec_len = compute_rec_len(curr_dir->name_len);

      if (curr_dir->rec_len != orig_rec_len){
        i+= orig_rec_len;
        struct ext2_dir_entry *found_file =
              (struct ext2_dir_entry*) ((unsigned char *) disk
              + (1024*restore_inode->i_block[0])+i);

        //Check for the restored file being a directory
        if (found_file->file_type == EXT2_FT_DIR
          && strncmp(found_file->name, file_name, strlen(file_name)) == 0){
          fprintf(stderr, "Cannot restore directory.\n");
          return (-EISDIR);
        }

        //If the file to be restored is found, restore it
        else if(strncmp(found_file->name, file_name, strlen(file_name)) == 0){
          struct ext2_inode *file_inode =
                  (struct ext2_inode *) ((unsigned char *) disk
                              + (1024*gd->bg_inode_table) + ((found_file->inode-1) * 128));
          curr_dir->rec_len = orig_rec_len;
          sb->s_free_inodes_count--;
        	gd->bg_free_inodes_count--;
          file_inode->i_dtime = 0;
          file_inode->i_links_count = 1;
          update_bitmap(found_file->inode, gd->bg_inode_bitmap, disk);
          int j = 0;

          //Use the head block until capacity
          while(file_inode->i_block[j] != 0 && j < 12
            && file_inode->i_block[j] < sb->s_blocks_count){
            update_bitmap(file_inode->i_block[j], gd->bg_block_bitmap, disk);
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count--;;
            j++;
          }
          j = 0;

          //Use indirect blocks for additional memory if required
          if(file_inode->i_block[12] != 0 && file_inode->i_block[j] < sb->s_blocks_count){
            for(; j < EXT2_BLOCK_SIZE; j++){
              int * indirect_block = (int*)((unsigned char *)(disk + (EXT2_BLOCK_SIZE * file_inode->i_block[12])));
              if(indirect_block[j] > 0 && (int)indirect_block[j] < sb->s_blocks_count){
                update_bitmap((int)indirect_block[j], gd->bg_block_bitmap, disk);
                sb->s_free_blocks_count--;
                gd->bg_free_blocks_count--;
              }
            }

            //Update the bitmap metadata
            update_bitmap(file_inode->i_block[12], gd->bg_block_bitmap, disk);
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count--;
          }
          break;
        }

        //Check for length required is too large
        else if (i + found_file->rec_len == 1024){
          fprintf(stderr, "Cannot restore file.\n");
          return (-ENOENT);
        }
        i-= orig_rec_len;
      }
      i+= curr_dir->rec_len;
    }

}
