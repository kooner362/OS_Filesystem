#include "ext2.h"

#ifndef EXT2_FUNCTIONS_H
#define EXT2_FUNCTIONS_H


int find_slash(char* file_path);
int dir_len (char* file_path);
char* find_dir_name (char* file_path);
int* build_bitmap (int total, unsigned int block, unsigned char* disk);
int next_free (int* bitmap, int size);
void update_bitmap(int bit, int block, unsigned char* disk);
int compute_rec_len(int name_len);
char* get_parent(char* path);
char* path_finder (char* full_path, int n);
int find_inode_index(char* file_path, int num_inodes, int inode_table, unsigned char* disk);
void create_dir (int start_index, int inode_table, int new_index, int d_len, char* dir_name, unsigned char* disk);
void create_file (int start_index, int inode_table, int new_index, int d_len, char* dir_name, unsigned char* disk);
int blocks_needed(int file_size);
int file_type(char* file_path, int num_inodes, int inode_table, unsigned char* disk);
int inode_index (char* file_path, int num_inodes, int inode_table, unsigned char* disk);
void rm_update_bitmap(int bit, int block, unsigned char* disk);
unsigned char inode_file_mode(unsigned short file_mode);
int count_free_bitmap(int* bitmap, int size);


#endif
