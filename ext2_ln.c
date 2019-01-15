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
    if(argc < 4 || argc > 5 || strlen(argv[3]) < 2) {
        fprintf(stderr, "Usage: %s <image file name> <flag> <file> <dir path>\n", argv[0]);
        exit(1);
    }

    //Open the image with a call to mmap
    int fd = open(argv[1], O_RDWR);
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    //Check for a correct call to mmap
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    //Path variables
    char *file_path;
    char *dir_path;
    int idx_offset = 0;

    //If the user used -s, activate the index offset for referencing argv
    if(argc == 5 && strlen(argv[2]) == 2 && argv[2][0] == '-' && argv[2][1] == 's'){
      idx_offset = 1;
    }

    //Get file name of file to link if user used <path> instead of /<path>
    if(argv[2+idx_offset][0] != '/'){
       file_path = malloc(sizeof(char)*strlen(argv[2+idx_offset]) + 2);
       file_path[0] = '/';
       strncpy(&file_path[1], argv[2+idx_offset], strlen(argv[2+idx_offset]));
       file_path[strlen(argv[2+idx_offset]) + 1] = '\0';
    }
    //Get file name of file to link if the user used /<path>
    else{
      file_path = malloc(sizeof(char)*strlen(argv[2+idx_offset]) + 1);
      strncpy(file_path, argv[2+idx_offset], strlen(argv[2+idx_offset]));
      file_path[strlen(argv[2+idx_offset])] = '\0';
    }

    //Get file name of directory to link if user used <path> instead of /<path>
    if(argv[3+idx_offset][0] != '/'){
       dir_path = malloc(sizeof(char)*strlen(argv[3+idx_offset]) + 2);
       dir_path[0] = '/';
       strncpy(&dir_path[1], argv[3+idx_offset], strlen(argv[3+idx_offset]));
       dir_path[strlen(argv[3+idx_offset]) + 1] = '\0';
    }

    //Get file name of directory to link if user used /<path>
    else{
      dir_path = malloc(sizeof(char)*strlen(argv[3]) + 1);
      strncpy(dir_path, argv[3+idx_offset], strlen(argv[3+idx_offset]));
      dir_path[strlen(argv[3+idx_offset])] = '\0';
    }

    //Create superblock and group block structures
    struct ext2_super_block *sb = (struct ext2_super_block *)
    ((unsigned char*) disk + EXT2_BLOCK_SIZE);
    struct ext2_group_desc *gd = (struct ext2_group_desc*)
    (disk + EXT2_BLOCK_SIZE * 2);
    
    //Get the indices for the inodes of the destination & source to be linked
    int dest_index = find_inode_index(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);
    int source_index = find_inode_index(file_path, sb->s_inodes_count, gd->bg_inode_table, disk);
    int num = file_type(dir_path, sb->s_inodes_count, gd->bg_inode_table, disk);

    //Check for link already existing
    if (dest_index == -2 && num == EXT2_FT_REG_FILE){
      fprintf(stderr, "Link name already exists.\n");
      exit(EEXIST);
    }

    //Check for the source not existing
    else if(source_index > 0){
      fprintf(stderr, "Source file doesn't exist.\n");
      exit(ENOENT);
    }

    //Check for linking to a directory
    else if(num == EXT2_FT_DIR){
      fprintf(stderr, "Refers to directory\n");
      exit(EISDIR);
    }

    //If flag was selected, create a symlink
    if(argc == 5 && strlen(argv[2]) == 2 && argv[2][0] == '-' && argv[2][1] == 's'){

      //Create inode & block bitmaps from the disk
      int *inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
      int *block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);
      
      //Find free space in the disk
      int next_inode = next_free(inode_bitmap, sb->s_inodes_count);
      int next_block = next_free(block_bitmap, sb->s_blocks_count);
      char* new_dir_name = find_dir_name (dir_path);

      //Update the counters
      sb->s_free_inodes_count--;
      sb->s_free_blocks_count--;
      gd->bg_free_inodes_count--;
      gd->bg_free_blocks_count--;
      update_bitmap(next_inode, gd->bg_inode_bitmap, disk);
      update_bitmap(next_block, gd->bg_block_bitmap, disk);

      //Set up the new inodes for the link
      struct ext2_inode *parent_inode = (struct ext2_inode *)
      ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((dest_index - 1) * 128));
      struct ext2_inode *new_inode = (struct ext2_inode *)
      ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((next_inode - 1) * 128));
      new_inode->i_mode = EXT2_S_IFLNK;
      new_inode->i_uid = 0;
      new_inode->i_size = strlen(argv[3]);
      new_inode->i_dtime = 0;
      new_inode->i_gid = 0;
      new_inode->i_links_count = 1;
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

      //Set a symlink pointer at the newly created block/inode
      void * sym_ptr = (void *) ((unsigned char*) (disk + (EXT2_BLOCK_SIZE*next_block)));
      memcpy(sym_ptr, (void*) argv[3], sizeof(char)*strlen(argv[3]));

      //Reset the metadata
      for(int i=0; i < EXT2_BLOCK_SIZE;){
        //
        struct ext2_dir_entry *curr_dir =
              (struct ext2_dir_entry*) ((unsigned char *) disk
              + (1024*parent_inode->i_block[0]) + i);

        //
        if(i+curr_dir->rec_len == EXT2_BLOCK_SIZE){
          curr_dir->rec_len = compute_rec_len(curr_dir->name_len);
          i+= curr_dir->rec_len;
          curr_dir = (struct ext2_dir_entry*) ((unsigned char *) disk
                     + (1024*parent_inode->i_block[0]) + i);

          //Set the current directory's name to the new directory's name
          for(int j=0; j< strlen(new_dir_name); j++){
            curr_dir->name[j] = new_dir_name[j];
          }

          //Iterate to the next inode
          curr_dir->inode = next_inode;
          curr_dir->rec_len = EXT2_BLOCK_SIZE - i;
          curr_dir->name_len = strlen(new_dir_name);
          curr_dir->file_type = EXT2_FT_SYMLINK;
        }
        i+= curr_dir->rec_len;
      }

      //Free the heap memory
      free(block_bitmap);
      free(inode_bitmap);
    }

    //Create a normal link if there's no -s flag
    else {
      //Find the inode and directory 
      int src_inode = inode_index (file_path, sb->s_inodes_count, gd->bg_inode_table, disk);
      char* name = find_dir_name(dir_path);
      int j = 0;
      struct ext2_inode *curr_inode = (struct ext2_inode *) ((unsigned char *) disk +
      (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((dest_index - 1) * 128));
      struct ext2_inode *main_inode = (struct ext2_inode *) ((unsigned char *) disk +
      (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((src_inode - 1) * 128));
      main_inode->i_links_count++;

      for (j=0; j<EXT2_BLOCK_SIZE;){
        struct ext2_dir_entry *curr_dir =
              (struct ext2_dir_entry*) ((unsigned char *) disk
              + (1024*curr_inode->i_block[0])+j);

        if (j + curr_dir->rec_len == EXT2_BLOCK_SIZE) {
          curr_dir->rec_len = compute_rec_len(curr_dir->name_len);
          j+= curr_dir->rec_len;

          curr_dir = (struct ext2_dir_entry*) ((unsigned char *) disk 
                     + (1024*curr_inode->i_block[0]) + j);

          for(int i=0; i < strlen(name); i++){
            curr_dir->name[i] = name[i];
          }
          curr_dir->inode = src_inode;
          curr_dir->rec_len = EXT2_BLOCK_SIZE - j;
          curr_dir->file_type = EXT2_FT_REG_FILE;
          curr_dir->name_len = strlen(name);
        }
        j+= curr_dir->rec_len;
      }
    }
}
