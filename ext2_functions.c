#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include "math.h"
#include <errno.h>
#include "ext2_functions.h"
#include "string.h"

/**
Converts inode file mode to a directory file type.
**/
unsigned char inode_file_mode(unsigned short file_mode){
  unsigned char mode = 0;
  //Directory
  if (file_mode & EXT2_S_IFDIR){
    mode = EXT2_FT_DIR;
  }
  //Regular file
  else if (file_mode & EXT2_S_IFREG){
    mode = EXT2_FT_REG_FILE;
  }
  //System link
  else if(file_mode & EXT2_S_IFLNK){
    mode = EXT2_FT_SYMLINK;
  }
  return mode;
}

/**
Returns the number of free blocks in bitmap
**/
int count_free_bitmap(int* bitmap, int size){
  int count = 0;
  for(int i=0; i < size; i ++){
    if (bitmap[i] == 0){
      count++;
    }
  }
  return count;
}

/**
Returns the index of the first slash in file_path
**/
int find_slash(char* file_path){
  int i;
  int last = 0;
  for(i=0; i < strlen(file_path); i++){
    if (file_path[i] == '/' && i != strlen(file_path) - 1){
      last = i;
    }
  }
  return last;
}

/**
Returns the length of the file_path
**/
int dir_len (char* file_path){
  int last_slash = find_slash(file_path);
  int dir_name_len;
  //Path includes '/' at the end
  if(file_path[strlen(file_path)-1] == '/'){
    dir_name_len = strlen(file_path) - (last_slash + 2);
  }
  //Path doesn't include '/' at the end
  else{
    dir_name_len = strlen(file_path) - (last_slash + 1);
  }
  return dir_name_len;
}

/**
Returns the dir name given the file_path
**/
char* find_dir_name (char* file_path){
  int last_slash = find_slash(file_path);
  int dir_name_len = dir_len(file_path);
  char* temp = malloc(sizeof(char) * dir_name_len + 1);
  strncpy(temp, &file_path[last_slash + 1], dir_name_len);
  temp[dir_name_len] = '\0';
  return temp;
}

/**
Builds a inode/block bitmap.
**/
int* build_bitmap (int total, unsigned int block, unsigned char* disk){
  unsigned char *iblock = (unsigned char *) (disk + (1024*block));
  int* bitmap = malloc(sizeof(int)*total);
  int count = 0;
  for (int i=0; i < total/8; i++){
    int byte = iblock[i];
    for(int bit=0; bit < 8; bit++){
      bitmap[count] = (byte%2);
      byte = byte/2;
      count++;
    }
  }
  return bitmap;
}

/**
Returns the next free block/inode.
**/
int next_free (int* bitmap, int size){
  int i;
  int found = -1;
  for(i = 0; i < size; i++){
    if (bitmap[i] == 0){
      found = i;
      break;
    }
  }
  return found + 1;
}

/**
Sets bit to 1 in the bitmap
**/
void update_bitmap(int bit, int block, unsigned char* disk){
  unsigned char *iblock = (unsigned char *) (disk + (1024*block));
  bit -= 1;
  int index = floor(bit/8);
  int new_val = 0;
  new_val += iblock[index];
  new_val += pow(2, bit%8);
  iblock[index] = new_val;
}

/**
Sets bit to 0 in the bitmap
**/
void rm_update_bitmap(int bit, int block, unsigned char* disk){
  unsigned char *iblock = (unsigned char *) (disk + (1024*block));
  bit -= 1;
  int index = floor(bit/8);
  int new_val = 0;
  new_val += iblock[index];
  new_val -= pow(2, bit%8);
  iblock[index] = new_val;
}

/**
Computers rec_len according to name length
**/
int compute_rec_len(int name_len){
  int size = 8 + name_len;
  while (size%4 != 0){
    size += 1;
  }
  return size;
}

/**
Returns the parent directory for path
**/
char* get_parent(char* path){
  int i;
  int last_slash = find_slash(path);
  int parent_index = 0;
  int parent_len = 0;
  char* parent;
  char root[] = "root";
  //Current directory is the root
  if(last_slash == 0){
    parent = malloc(sizeof(char)*5);
    strncpy(parent, root, 4);
    parent[5] = '\0';
  }
  else{
    for(i=0; i < last_slash; i++){
      if (path[i] == '/'){
        parent_index = i;
      }
    }
    parent_len = last_slash - parent_index;
    parent = malloc(sizeof(char)*parent_len);
    strncpy(parent, &path[parent_index+1], parent_len);
    parent[parent_len-1] = '\0';
  }
  return parent;
}

/**
Returns the nth dir name in full_path
**/
char* path_finder (char* full_path, int n){
  int index = -1;
  int count = 0;
  int prev = 0;
  char* temp;

  if(n>0){
    for(int i = 1; i < strlen(full_path); i++){
      //Directory
      if((i != strlen(full_path) - 1) && full_path[i] == '/'){
        count++;
        if(count == n){
          index = i;
          break;
        }
        else{
          prev = i;
        }
      }
      //End of path, didn't include '/' at end
      else if((i == strlen(full_path) - 1) && full_path[i] != '/'){
        count++;
        if(count == n){
          index = i+1;
        }
      }
      //End of path, included '/' at end
      else if ((i == strlen(full_path) - 1) && full_path[i] == '/'){
        count++;
        if(count == n){
          index = i;
        }
      }
    }
  }
  if(index != -1){
    temp = malloc(sizeof(char) * (index - prev));
    strncpy(temp, &full_path[prev+1], index-prev-1);
    temp[index-prev-1] = '\0';
  }
  else{
    temp = malloc(sizeof(char) * 2);
    temp[0] = ' ';
    temp[1] = '\0';
  }

  return temp;
}

/**
Returns the inode index to place next file/dir if it doesn't exist already
**/
int find_inode_index(char* file_path, int num_inodes, int inode_table, unsigned char* disk){
  int index = -1;
  int last_slash  = find_slash(file_path);
  int i = 1;
  int j;
  int curr_inode_index = 1;

  //Inode is in the root directory
  if (last_slash == 0){
    char* curr_dir_name = find_dir_name (file_path);
    struct ext2_inode *curr_inode =
            (struct ext2_inode *) ((unsigned char *) disk
                        + (1024*inode_table) + 128);
    for(j=0; j<1024;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);

      if(strncmp(curr_dir->name, curr_dir_name, curr_dir->name_len) == 0 &&
                            curr_dir->name_len == strlen(curr_dir_name)){
        return -2;//EEXIST
      }
      j+= curr_dir->rec_len;
    }
    return 2;
  }

  //Inode not in root directory
  else{
    char* curr_dir_name = path_finder(file_path, i);
    char* next_dir_name = path_finder(file_path, i+1);

    //While the path is non-empty
    while (curr_dir_name[0] != ' '){
      struct ext2_inode *curr_inode =
              (struct ext2_inode *) ((unsigned char *) disk
                          + (1024*inode_table) + (curr_inode_index * 128));
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]));

      //Find the file by looking through the directory contents
      if(curr_inode->i_size > 0 && curr_dir->inode < num_inodes && curr_dir->inode > 0){
        for(j=0; j < 1024;){

          curr_dir =
                (struct ext2_dir_entry*) ((unsigned char *) disk
                + (1024*curr_inode->i_block[0]) + j);

          if(strncmp(curr_dir->name, curr_dir_name, curr_dir->name_len) == 0 &&
                                curr_dir->name_len == strlen(curr_dir_name)){

            //Check for file not existing
            if(next_dir_name[0] == ' '){
              index = -2;//EEXIST
              break;
            }

            //Set the correct inode index to return 
            else{
              index = curr_dir->inode;
              curr_inode_index = curr_dir->inode-1;
              j=1024;
            }
          }
          //Check for end of space
          else if (j+curr_dir->rec_len == 1024 && next_dir_name[0] != ' '){
            index = -1;
          }
          j += curr_dir->rec_len;
        }
      }
      i++;


      //Free heap memory
      free(curr_dir_name);
      curr_dir_name = path_finder(file_path, i);
      free(next_dir_name);
      next_dir_name = path_finder(file_path, i+1);

    }
    free(curr_dir_name);
    free(next_dir_name);
  }
  return index;
}

/**
Creates directory
**/
void create_dir (int start_index, int inode_table, int new_index, int d_len, char* dir_name, unsigned char* disk){
  //Create a set of inodes to keep track of important indices
  struct ext2_inode *root_inode = (struct ext2_inode *) ((unsigned char *) disk + (1024*inode_table) + (128*start_index));
  struct ext2_inode *new_inode = (struct ext2_inode *) ((unsigned char *) disk + (1024*inode_table) + (128*new_index));
  struct ext2_dir_entry *root_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*root_inode->i_block[0]));
  struct ext2_dir_entry *new_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*new_inode->i_block[0]));
  root_inode->i_links_count += 1;
  for(int i = 0; i < 1024;){
    root_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*root_inode->i_block[0]) + i);

    //Find a free space for the directory
    if (i + root_dir->rec_len == 1024){
      root_dir->rec_len = compute_rec_len(root_dir->name_len);
      i += root_dir->rec_len;
      root_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*root_inode->i_block[0]) + i);
      root_dir->inode = new_index+1;
      root_dir->rec_len = 1024-i;
      root_dir-> name_len = d_len;
      root_dir->file_type = EXT2_FT_DIR;
      for(int j = 0; j < d_len; j++){
        root_dir->name[j] = dir_name[j];
      }
    }
    i+= root_dir->rec_len;
  }
  //Initialize the new directory
  new_dir->rec_len = 12;
  new_dir->inode = new_index+1;
  new_dir->name_len = 1;
  new_dir->name[0] = '.';
  new_dir->file_type = EXT2_FT_DIR;
  new_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*new_inode->i_block[0]) + 12);
  new_dir->rec_len = 1024 - 12;
  new_dir->inode = start_index+1;
  new_dir->name_len = 2;
  new_dir->file_type = EXT2_FT_DIR;
  new_dir->name[0] = '.';
  new_dir->name[1] = '.';
}

/**
Returns the blocks needed based on file_size
**/
int blocks_needed(int file_size){
  int blocks = (int) ceil((float)file_size/EXT2_BLOCK_SIZE);
  if(blocks <= 12){
    return blocks;
  }
  else{
    return blocks+1;
  }
}

/**
Creates file
**/
void create_file (int start_index, int inode_table, int new_index, int d_len, char* dir_name, unsigned char* disk){
  struct ext2_inode *root_inode = (struct ext2_inode *) ((unsigned char *) disk + (1024*inode_table) + (128*start_index));
  struct ext2_dir_entry *root_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*root_inode->i_block[0]));
  for(int i = 0; i < 1024;){
    root_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*root_inode->i_block[0]) + i);

    //Find a place in memory and inode indices for the new file and create it
    if (i + root_dir->rec_len == 1024){
      root_dir->rec_len = compute_rec_len(root_dir->name_len);
      i += root_dir->rec_len;
      root_dir = (struct ext2_dir_entry*) ((unsigned char *) disk + (1024*root_inode->i_block[0]) + i);
      root_dir->inode = new_index+1;
      root_dir->rec_len = 1024-i;
      root_dir-> name_len = d_len;
      root_dir->file_type = EXT2_FT_REG_FILE;
      for(int j = 0; j < d_len; j++){
        root_dir->name[j] = dir_name[j];
      }
    }
    i+= root_dir->rec_len;
  }
}

/**
Returns the file_type of file/dir in the file_path
**/
int file_type(char* file_path, int num_inodes, int inode_table, unsigned char* disk){
  int last_slash  = find_slash(file_path);
  int i = 1;
  int j;
  int ftype = -1;
  int curr_inode_index = 1;

  //If the file is in the root directory
  if (last_slash == 0){
    char* curr_dir_name = find_dir_name (file_path);
    struct ext2_inode *curr_inode =
            (struct ext2_inode *) ((unsigned char *) disk
                        + (1024*inode_table) + 128);
    for(j=0; j<1024;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);

      //If the file was found, return the file type
      if(strncmp(curr_dir->name, curr_dir_name, curr_dir->name_len) == 0 &&
                            curr_dir->name_len == strlen(curr_dir_name)){
        ftype = (int) curr_dir->file_type;
      }
      j+= curr_dir->rec_len;
    }
  }
  else{
    char* curr_dir_name = path_finder(file_path, i);
    char* next_dir_name = path_finder(file_path, i+1);

    //Find the inode for the file
    while (curr_dir_name[0] != ' '){
      struct ext2_inode *curr_inode =
              (struct ext2_inode *) ((unsigned char *) disk
                          + (1024*inode_table) + (curr_inode_index * 128));
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]));

      if(curr_inode->i_size > 0 && curr_dir->inode < num_inodes && curr_dir->inode > 0){
        for(j=0; j < 1024;){

          curr_dir =
                (struct ext2_dir_entry*) ((unsigned char *) disk
                + (1024*curr_inode->i_block[0]) + j);

          //Search through all the directories to find the correct inode
          if(strncmp(curr_dir->name, curr_dir_name, curr_dir->name_len) == 0 &&
                                curr_dir->name_len == strlen(curr_dir_name)){
            //Set the file type to return
            if(next_dir_name[0] == ' '){
              ftype = (int) curr_dir->file_type;
              //index = -2;//EEXIST
              break;
            }
            else{
              //index = curr_dir->inode;
              curr_inode_index = curr_dir->inode-1;
              j=1024;
            }
          }
          else if (j+curr_dir->rec_len == 1024 && next_dir_name[0] != ' '){
            //index = -1;
          }
          j += curr_dir->rec_len;
        }
      }
      i++;

      //Free the used memory
      free(curr_dir_name);
      curr_dir_name = path_finder(file_path, i);
      free(next_dir_name);
      next_dir_name = path_finder(file_path, i+1);

    }
    free(curr_dir_name);
    free(next_dir_name);
  }
  return ftype;
}

/**
Returns inode index of the file/dir in file_path
**/
int inode_index (char* file_path, int num_inodes, int inode_table, unsigned char* disk){
  int index = -1;
  int last_slash  = find_slash(file_path);
  int i = 1;
  int j;
  int curr_inode_index = 1;
  //If the file is in the root directory
  if (last_slash == 0){
    char* curr_dir_name = find_dir_name (file_path);
    struct ext2_inode *curr_inode =
            (struct ext2_inode *) ((unsigned char *) disk
                        + (1024*inode_table) + 128);
    //Look through the directory for the file
    for(j=0; j<1024;){
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]) + j);

      //If the file is found, set the index to return
      if(strncmp(curr_dir->name, curr_dir_name, curr_dir->name_len) == 0 &&
                            curr_dir->name_len == strlen(curr_dir_name)){
        index = curr_dir->inode;
      }
      j+= curr_dir->rec_len;
    }
  }
  else{
    char* curr_dir_name = path_finder(file_path, i);
    char* next_dir_name = path_finder(file_path, i+1);

    //Find the directory containing the file
    while (curr_dir_name[0] != ' '){
      struct ext2_inode *curr_inode =
              (struct ext2_inode *) ((unsigned char *) disk
                          + (1024*inode_table) + (curr_inode_index * 128));
      struct ext2_dir_entry *curr_dir =
            (struct ext2_dir_entry*) ((unsigned char *) disk
            + (1024*curr_inode->i_block[0]));
      if(curr_inode->i_size > 0 && curr_dir->inode < num_inodes && curr_dir->inode > 0){
        for(j=0; j < 1024;){

          curr_dir =
                (struct ext2_dir_entry*) ((unsigned char *) disk
                + (1024*curr_inode->i_block[0]) + j);
          //If the file is found set the index
          if(strncmp(curr_dir->name, curr_dir_name, curr_dir->name_len) == 0 &&
                                curr_dir->name_len == strlen(curr_dir_name)){
            if(next_dir_name[0] == ' '){
              index = curr_dir->inode;
              break;
            }
            //Its a directory, return the directory index
            else{
              index = curr_dir->inode;
              curr_inode_index = curr_dir->inode-1;
              break;
            }
          }
          //File not found
          else if (j+curr_dir->rec_len == 1024){
            index = -1;
            break;
          }
          j += curr_dir->rec_len;
        }
      }

      i++;

      //Free used memory
      free(curr_dir_name);
      curr_dir_name = path_finder(file_path, i);
      free(next_dir_name);
      next_dir_name = path_finder(file_path, i+1);
      if(index == -1){
        break;
      }
    }
    free(curr_dir_name);
    free(next_dir_name);
  }
  return index;
}
