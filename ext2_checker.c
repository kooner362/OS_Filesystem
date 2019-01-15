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
int total_fixes = 0;

//Check if the disk's metadata is correct
void block_checker(){

  //Initialize data structures required to modify the image
  struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
  //Initialize bitmaps required to compare the image
  int *inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
  int *block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);
  //Get the free space in both bitmaps
  int free_inode_count = count_free_bitmap(inode_bitmap, sb->s_inodes_count);
  int free_block_count = count_free_bitmap(block_bitmap, sb->s_blocks_count);

  //Check for incorrect free inode count in the superblock 
  if (free_inode_count != sb->s_free_inodes_count){
    //Get the difference between the superblock & bitmap inode counters and set them equal
    int diff = abs((int)sb->s_free_inodes_count - free_inode_count);
    sb->s_free_inodes_count = free_inode_count;
    total_fixes +=  diff;
    printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", diff);
  }

  //Check for an incorrect free inode count in the block group
  if(free_inode_count != gd->bg_free_inodes_count){
    //Get the difference between the block group & bitmap inode counters and set them equal
    int diff = abs((int)sb->s_free_inodes_count - free_inode_count);
    gd->bg_free_inodes_count = free_inode_count;
    total_fixes +=  diff;
    printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", diff);
  }

  //Check for an incorrect free block count in the superblock
  if (free_block_count != sb->s_free_blocks_count){
    //Get the difference between the superblock & bitmap free block counters and set them equal
    int diff = abs((int)sb->s_free_blocks_count - free_block_count);
    sb->s_free_blocks_count = free_block_count;
    total_fixes +=  diff;
    printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", diff);
  }

  //Check for an incorrect free block count in the block group
  if(free_block_count != gd->bg_free_blocks_count){
    //Get the difference between the block group & bitmap free block counters and set them equal
    int diff = abs((int)sb->s_free_blocks_count - free_block_count);
    gd->bg_free_blocks_count = free_block_count;
    total_fixes +=  diff;
    printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", diff);
  }

  //Free the bitmaps before exiting
  free(inode_bitmap);
  free(block_bitmap);
}

//
void file_mode_checker(){
  struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
  struct ext2_inode *curr_inode;
  struct ext2_inode *dir_inode;

  //Go over all the inodes in the disk
  for(int i = 1; i <= sb->s_inodes_count; i++){
    curr_inode =
    (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));

    //
    while ((curr_inode->i_block[0] == 0 || curr_inode->i_block[0] > sb->s_blocks_count) && i <= sb->s_inodes_count){
      i++;
      curr_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    }
    for (int j=0; j < EXT2_BLOCK_SIZE;){

      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);
      if(curr_dir->inode == 0 || curr_dir->inode > sb->s_inodes_count){
        break;
      }
      dir_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((curr_dir->inode-1) * 128));
       unsigned char f_mode = inode_file_mode(dir_inode->i_mode);
       if(curr_dir->file_type != f_mode && dir_inode->i_mode != 0){
        curr_dir->file_type = f_mode;
        total_fixes++;
        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", curr_dir->inode);
      }
      j+= curr_dir->rec_len;
    }
    if (i == 1){
      i = EXT2_GOOD_OLD_FIRST_INO-1;
    }
  }

}

void valid_bitmap(){
  struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
  struct ext2_inode *curr_inode;
  int *inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);

  for(int i = 1; i <= sb->s_inodes_count; i++){
    curr_inode =
    (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    while ((curr_inode->i_block[0] == 0 || curr_inode->i_block[0] > sb->s_blocks_count) && i <= sb->s_inodes_count){
      i++;
      curr_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    }
    for (int j=0; j < EXT2_BLOCK_SIZE;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);
        if(curr_dir->inode == 0 || curr_dir->inode > sb->s_inodes_count){
          break;
        }
       if(inode_bitmap[curr_dir->inode-1] != 1){
         update_bitmap(curr_dir->inode, gd->bg_inode_bitmap, disk);
         free(inode_bitmap);
         inode_bitmap = build_bitmap(sb->s_inodes_count, gd->bg_inode_bitmap, disk);
         sb->s_free_inodes_count--;
         gd->bg_free_inodes_count--;
         total_fixes++;
         printf("Fixed: inode [%d] not marked as in-use\n", i+1);
       }
      j+= curr_dir->rec_len;
    }
    if (i == 1){
      i = EXT2_GOOD_OLD_FIRST_INO-1;
    }
  }
  free(inode_bitmap);
}

void dtime_checker(){
  struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
  struct ext2_inode *curr_inode;
  struct ext2_inode *dir_inode;
  for(int i = 1; i <= sb->s_inodes_count; i++){
    curr_inode =
    (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    while ((curr_inode->i_block[0] == 0 || curr_inode->i_block[0] > sb->s_blocks_count) && i <= sb->s_inodes_count){
      i++;
      curr_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    }
    for (int j=0; j < EXT2_BLOCK_SIZE;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);
      if(curr_dir->inode == 0 || curr_dir->inode > sb->s_inodes_count){
        break;
      }
      dir_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((curr_dir->inode-1) * 128));


       if(dir_inode->i_dtime != 0){
         dir_inode->i_dtime = 0;
         total_fixes++;
         printf("Fixed: valid inode marked for deletion: [%d]\n", curr_dir->inode);
       }
      j+= curr_dir->rec_len;
    }
    if (i == 1){
      i = EXT2_GOOD_OLD_FIRST_INO-1;
    }
  }
}

void dblock_checker(){
  struct ext2_super_block *sb = (struct ext2_super_block *)((unsigned char *)disk + 1024);
  struct ext2_group_desc *gd = (struct ext2_group_desc *)((unsigned char *)disk + 2048);
  int *block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);
  struct ext2_inode *curr_inode;

  for(int i = 1; i <= sb->s_inodes_count; i++){
    int fixes = 0;
    curr_inode =
    (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    while ((curr_inode->i_block[0] == 0 || curr_inode->i_block[0] > sb->s_blocks_count) && i <= sb->s_inodes_count){
      i++;
      curr_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + (i * 128));
    }
    for (int j=0; j < EXT2_BLOCK_SIZE;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);
      if(curr_dir->inode == 0 || curr_dir->inode > sb->s_inodes_count){
        break;
      }
      struct ext2_inode *dir_inode =
      (struct ext2_inode *) ((unsigned char *) disk + (EXT2_BLOCK_SIZE*gd->bg_inode_table) + ((curr_dir->inode-1) * 128));

      int block_req = blocks_needed(dir_inode->i_size);
      if(block_req <= 12){
        block_req++;
      }
      for(int k=0; k < block_req - 1; k++){
        int block_bit = -1;
        if (k <= 12){
          block_bit = dir_inode->i_block[k];
        }
        else if (k > 12){
          int * indirect_block = (int*)((unsigned char *)(disk + (EXT2_BLOCK_SIZE * dir_inode->i_block[12])));
          block_bit = indirect_block[k-13];
          if(block_bit == 0 || block_bit >= sb->s_blocks_count){
            block_bit = -1;
            break;
          }
        }
        if (block_bitmap[block_bit-1] == 0 && block_bit != -1){
          update_bitmap(block_bit, gd->bg_block_bitmap, disk);
          free(block_bitmap);
          block_bitmap = build_bitmap(sb->s_blocks_count, gd->bg_block_bitmap, disk);
          sb->s_free_blocks_count--;
          gd->bg_free_blocks_count--;
          total_fixes++;
          fixes++;
        }
      }
      j+= curr_dir->rec_len;
      if(j == EXT2_BLOCK_SIZE && fixes > 0){
        printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", fixes, curr_dir->inode);
      }
    }
  }
}


int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    block_checker();
    valid_bitmap();
    file_mode_checker();
    dtime_checker();
    dblock_checker();
    if(total_fixes > 0){
      printf("%d file system inconsistencies repaired!\n", total_fixes);
    }
    else{
      printf("No file system inconsistencies detected!");
    }
}
