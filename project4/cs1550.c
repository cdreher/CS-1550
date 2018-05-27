/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
	//The next disk block, if needed. This is the next pointer in the linked
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

#define MAX_FAT_ENTRIES_IN_BLOCK (BLOCK_SIZE/sizeof(short))

struct cs1550_file_alloc_table {
	short table[MAX_FAT_ENTRIES_IN_BLOCK];
};

typedef struct cs1550_file_alloc_table cs1550_fat_block;
#define START_BLOCK 2

static cs1550_root_directory getRoot(void);
static cs1550_fat_block getFat(void);

//get the root of the disk
static cs1550_root_directory getRoot(){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, 0, SEEK_SET);

	cs1550_root_directory r;
	fread(&r, BLOCK_SIZE, 1, disk);

	return r;
}

//get the FAT from the disk.
static cs1550_fat_block getFat(){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, 0, SEEK_SET);

	cs1550_fat_block f;
	fread(&f, BLOCK_SIZE, 1, disk);

	return f;
}

//write to root.
static void writeToRoot(cs1550_root_directory* root){
	FILE* disk = fopen(".disk", "r+b");
	fwrite(root, BLOCK_SIZE, 1, disk);
	fclose(disk);
}

//write to FAT.
static void writeToFAT(cs1550_fat_block* fat){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, BLOCK_SIZE, SEEK_SET);
	fwrite(fat, BLOCK_SIZE, 1, disk);
	fclose(disk);
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
 static int cs1550_getattr(const char *path, struct stat *stbuf)
 {
  int res = 0;

	//Create strings.
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];

	//Clear strings.
	strcpy(directory, "");
	strcpy(filename, "");
	strcpy(extension, "");

  if(strlen(path) != 1){
 	 sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
  }

  if(strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION){
 	 return -ENAMETOOLONG;
  }

  memset(stbuf, 0, sizeof(struct stat));

	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
 	 res = 0;
 	 return res;
  }
	else {
		if(strlen(directory)==0){
			res = -ENOENT;			//return path doesnt exist.
			return res;					//failure
		}
		 else{
 		 int i = 0;
		 struct cs1550_directory dir;
			strcpy(dir.dname, "");
			dir.nStartBlock = -1;

			cs1550_root_directory root = getRoot();

			for(i = 0; i < root.nDirectories; i++){			//iterate through all directories.
				struct cs1550_directory current = root.directories[i];		//get current directory.

				//check to see if the current directory is the one we want.
				if(strcmp(current.dname, directory) == 0){
					dir = current;
					break;
				}
			}

 		 if(strcmp(dir.dname, "") == 0){			//no directory found.
 			 res = -ENOENT;
 			 return res;			//failure
 		 }

 		 if(strcmp(filename, "") == 0){  		//looking for directory, not a file.
 			 res = 0;
 			 stbuf->st_mode = S_IFDIR | 0755;
 			 stbuf->st_nlink = 2;
 			 return res;			//successful
 		 }

		 //-- DIRECTORY WAS FOUND...now we have to check the files in it --//
			FILE* disk = fopen(".disk", "r+b");
			int loc = BLOCK_SIZE * dir.nStartBlock;
			fseek(disk, loc, SEEK_SET);

			cs1550_directory_entry entry;
			entry.nFiles = 0;
			memset(entry.files, 0, MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory));

			int found = fread(&entry, BLOCK_SIZE, 1, disk);
			fclose(disk);

			if(found == 1){
 				//create empty file.
 				struct cs1550_file_directory file;
 				file.fsize = 0;
 				file.nStartBlock = -1;
 				strcpy(file.fname, "");
 				strcpy(file.fext, "");

				//Iterate through all the files in this directory.
 				int i=0;
 				for(i = 0; i < MAX_FILES_IN_DIR; i++){
 					struct cs1550_file_directory current = entry.files[i];

 					//if current file is the file we want...
 					if(strcmp(current.fname, filename) == 0 && strcmp(current.fext, extension) == 0){
 						file = current;
 						break;
 					}
 				}

				//no file was found...
 				if(file.nStartBlock == -1){
 					res = -ENOENT;
 					return res;
 				}
 				else{
 					res = 0;
 					stbuf->st_mode = S_IFREG | 0666;
 					stbuf->st_nlink = 1;
 					stbuf->st_size = file.fsize;
 					return res;				//successful
 				}
 		 }
 	 }
  }

  return res;
 }
/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	int length = strlen(path);
	char path_copy[length];
	strcpy(path_copy, path);

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	char* dest = strtok(path_copy, "/");
	char * filename = strtok(NULL, ".");
	char* ext = strtok(NULL, ".");

	if((dest && dest[0]) && strlen(dest) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}
	if((filename && filename[0]) && strlen(filename) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}
	if((ext && ext[0]) && strlen(ext) > MAX_EXTENSION){
		return -ENAMETOOLONG;
	}


	if(strcmp(path, "/") == 0){
		int i=0;

		cs1550_root_directory root = getRoot();			//get root
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){
			char* directory_name = root.directories[i].dname;
			if(strcmp(directory_name, "") != 0){
				filler(buf, directory_name, NULL, 0);
			}
		}
		return 0;
	}
	else {
		int i=0;

		//Create temp directory
		struct cs1550_directory temp;
		strcpy(temp.dname, "");
		temp.nStartBlock = -1;

		cs1550_root_directory root = getRoot();		//get root

		//check all directories for one that we mathc
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){
			if(strcmp(dest, root.directories[i].dname) == 0){
				temp = root.directories[i];
				break;
			}
		}

		//if nothing was found, return error
		if(strcmp(temp.dname, "") == 0){
			return -ENOENT;
		}
		//Read the directory jawn.
		else{
			FILE* disk = fopen(".disk", "r+b");
			int loc = BLOCK_SIZE * temp.nStartBlock;
			fseek(disk, loc, SEEK_SET);

			cs1550_directory_entry entry;
			entry.nFiles = 0;
			memset(entry.files, 0, MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory));

			fread(&entry, BLOCK_SIZE, 1, disk);
			fclose(disk);

			int j=0;

			//parse through all files in directory
			for(j = 0; j < MAX_FILES_IN_DIR; j++){
				struct cs1550_file_directory file_direct = entry.files[j];
				char filename_copy[MAX_FILENAME + 1];
				strcpy(filename_copy, file_direct.fname);

				if(strcmp(file_direct.fext, "") != 0){
					strcat(filename_copy, ".");
				}
				strcat(filename_copy, file_direct.fext);

				if(strcmp(file_direct.fname, "") != 0){			//is there is a file here, display to user
					filler(buf, filename_copy, NULL, 0);
				}
			}
		}
	}

	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;

	char* directory;
	char* sub;

	int length = strlen(path);
	char path_copy[length];
	strcpy(path_copy, path);

	directory = strtok(path_copy, "/");
	sub = strtok(NULL, "/");

	//return ENAMETOOLONG if the name is beyond 8 chars
	if(strlen(directory) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}
	//return EPERM if the directory is not under the root dir only
	else if(sub && sub[0]){
		return -EPERM;
	}

	cs1550_root_directory root = getRoot(); 			//get root
	cs1550_fat_block fat = getFat();							//get FAT

	//if maxed out on directories...
	if(root.nDirectories >= MAX_DIRS_IN_ROOT){
		return -EPERM;
	}

	int i = 0;
	for(i = 0; i < MAX_DIRS_IN_ROOT; i++){
		//check if the directory already exists...
		if(strcmp(root.directories[i].dname, directory) == 0){
			return -EEXIST;
		}

		//check to see if we have new directory, if so create one
		if(strcmp(root.directories[i].dname, "") == 0){
			struct cs1550_directory new_directory;
			strcpy(new_directory.dname, directory);

			int j = 0;
			//iterate over FAT to find first free block to store new directory
			for(j = START_BLOCK; j < MAX_FAT_ENTRIES_IN_BLOCK; j++){
				if(fat.table[j] == 0){
					fat.table[j] = EOF;
					new_directory.nStartBlock = j;
					break;
				}
			}

			FILE* disk = fopen(".disk", "r+b");
			int loc = BLOCK_SIZE * new_directory.nStartBlock;
			fseek(disk, loc, SEEK_SET);

			cs1550_directory_entry entry;
			entry.nFiles = 0;
			int items_found = fread(&entry, BLOCK_SIZE, 1, disk);

			if(items_found == 1){
				memset(&entry, 0, sizeof(struct cs1550_directory_entry));
				fwrite(&entry, BLOCK_SIZE, 1, disk);
				fclose(disk);

				root.nDirectories++;
				root.directories[i] = new_directory;

				writeToRoot(&root);
				writeToFAT(&fat);
			}
			else{
				fclose(disk);
			}
			return 0;

		}

	}

	return 0;
}


/*
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
 static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
 {
 	(void) mode;
 	(void) dev;

	char* directory;
	char* filename;
	char* ext;

	int length = strlen(path);
	char path_copy[length];
	strcpy(path_copy , path);

	directory = strtok(path_copy, "/");
	filename = strtok(NULL, ".");
	ext = strtok(NULL, ".");

	if((directory && directory[0]) && strcmp(directory, "") != 0){
		if(filename && filename[0]){
			//if filename is trying to be created in root...
			if(strcmp(filename, "") == 0){
				return -EPERM;
			}

 			if(ext && ext[0]){
				//if name is too long return ENAMETOOLONG
 				if(strlen(filename) > MAX_FILENAME || strlen(ext) > MAX_EXTENSION){
 					return -ENAMETOOLONG;
 				}
 			}
			else{
					//possible that there is no extension, so still check filename length
				if(strlen(filename) > MAX_FILENAME){
 					return -ENAMETOOLONG;
 				}
 			}
 		}
		else{
 			return -EPERM;
 		}

		cs1550_root_directory root = getRoot();
		cs1550_fat_block fat = getFat();
		struct cs1550_directory dir;

		int i= 0;
		//iterate through all directories in root
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){
 			struct cs1550_directory current_dir = root.directories[i];
			//if matching directory is found...
 			if(strcmp(directory, current_dir.dname) == 0){
 				dir = current_dir;
 				break;
 			}
 		}

		//valid directory was located successfully
 		if(strcmp(dir.dname, "") != 0){
 			//Read in the directory from disk
			FILE* disk = fopen(".disk", "r+b");
			long loc = BLOCK_SIZE * dir.nStartBlock;
			fseek(disk, loc, SEEK_SET);

 			cs1550_directory_entry entry;
 			int success = fread(&entry, BLOCK_SIZE, 1, disk);

			//if maxed out on files in directory
 			if(entry.nFiles >= MAX_FILES_IN_DIR){
 				return -EPERM;
 			}

 			if(success){
 				int exists = 0;
 				int freeFileIndex = -1;

 				int j = 0;
 				for(j = 0; j < MAX_FILES_IN_DIR; j++){
 					struct cs1550_file_directory curr_file_dir = entry.files[j];
 					if(strcmp(curr_file_dir.fname, "") == 0 && strcmp(curr_file_dir.fext, "") == 0 && freeFileIndex == -1){
 						freeFileIndex = j;
 					}

					//matched file already exists
 					if(strcmp(curr_file_dir.fname, filename) == 0 && strcmp(curr_file_dir.fext, ext) == 0){
 						exists = 1;
 						break;
 					}
 				}

 				if(!exists){
 					short fat_index = -1;

 					int k = 0;
 					for(k = 2; k < MAX_FAT_ENTRIES_IN_BLOCK; k++){
 						if(fat.table[k] == 0){
 							fat_index = k;
 							fat.table[k] = EOF;
 							break;
 						}
 					}

					//create new file.
 					struct cs1550_file_directory new_file;
 					strcpy(new_file.fname, filename);
 					if(ext && ext[0]) {
						strcpy(new_file.fext, ext);
					}

 					else {
						strcpy(new_file.fext, "");
 					}
					new_file.fsize = 0;
 					new_file.nStartBlock = fat_index;

 					entry.files[freeFileIndex] = new_file;
 					entry.nFiles++;

 					fseek(disk, loc, SEEK_SET);
 					fwrite(&entry, BLOCK_SIZE, 1, disk);

 					fclose(disk);

 					writeToRoot(&root);
 					writeToFAT(&fat);
 				}
				else {
 					fclose(disk);
 					return -EEXIST;
 				}
 			}
			else {
 				fclose(disk);
 				return -EPERM;
 			}
 		}
		else {
 			if(strcmp(directory, "") == 0){
 				return 0;
 			} else if(strcmp(filename, "") == 0){
 				return -EPERM;
 			}
 		}
 	}

 	return 0;
 }

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/*
 * Read size bytes from file into buf starting from offset
 * NOTE: Read is basically the same as cs1550_write, just slight modification in way its set up
 *
 */
 static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
 			  struct fuse_file_info *fi)
 {
 	(void) buf;
 	(void) offset;
 	(void) fi;
 	(void) path;


 	char* directory;
 	char* file_name;
 	char* file_ext;

 	int path_length = strlen(path);
 	char path_copy[path_length];
 	strcpy(path_copy, path);

 	directory = strtok(path_copy, "/");
 	file_name = strtok(NULL, ".");
 	file_ext = strtok(NULL, ".");

	//find directory that we are looking at...
	struct cs1550_directory_entry* entry = malloc(sizeof(struct cs1550_directory_entry));

	//Find correct file, and read it...
	int i = 0;
  int fileIndex = -1;
	for(i = 0; i < entry -> nFiles; i++)
	{
		if(strcmp(entry -> files[i].fname, file_name) == 0 && strcmp(entry -> files[i].fext, file_ext) == 0){
			fileIndex = i;
			break;
		}
	}

	if(size == 0){
		return 0;
	}

	//if offset is bigger than file size we have an error.
	if(offset > entry -> files[i].fsize){
		return -EFBIG;
	}

	int locationToRead = ((entry -> files[fileIndex].nStartBlock * 512) + offset);

	FILE* file = fopen(".disk", "r+b");
	fseek(file, locationToRead, SEEK_SET);
	int read = fread(buf, 1, size, file);
	fclose(file);

	return read;
 }

/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	char* directory;
	char* filename;
	char* ext;

	int length = strlen(path);
	char path_copy[length];
	strcpy(path_copy, path);

	directory = strtok(path_copy, "/");
	filename = strtok(NULL, ".");
	ext = strtok(NULL, ".");

	if((directory && directory[0]) && strcmp(directory, "") != 0){
		if(filename && filename[0]){
			//if filename is trying to be created in root...
			if(strcmp(filename, "") == 0){
				return -EEXIST;
			}

 			if(ext && ext[0]){
				//if name is too long return ENAMETOOLONG
 				if(strlen(filename) > MAX_FILENAME || strlen(ext) > MAX_EXTENSION){
 					return -ENAMETOOLONG;
 				}
 			}
			else{
					//possible that there is no extension, so still check filename length
				if(strlen(filename) > MAX_FILENAME){
 					return -ENAMETOOLONG;
 				}
 			}
 		}
		else{
 			return -EEXIST;
 		}

		cs1550_root_directory root = getRoot();
		cs1550_fat_block fat = getFat();
		struct cs1550_directory dir;

		int i= 0;
		//iterate through all directories in root
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){
 			struct cs1550_directory current_dir = root.directories[i];
			//if matching directory is found...
 			if(strcmp(directory, current_dir.dname) == 0){
 				dir = current_dir;
 				break;
 			}
 		}

		//valid directory was located successfully
 		if(strcmp(dir.dname, "") != 0){
 			//Read in the directory from disk
			FILE* disk = fopen(".disk", "r+b");
			long loc = BLOCK_SIZE * dir.nStartBlock;
			fseek(disk, loc, SEEK_SET);

 			cs1550_directory_entry entry;
 			int success = fread(&entry, BLOCK_SIZE, 1, disk);
			fclose(disk);

 			if(success){
 				int freeFileIndex = -1;
				struct cs1550_file_directory file;

 				int j = 0;
 				for(j = 0; j < MAX_FILES_IN_DIR; j++){
 					struct cs1550_file_directory curr_file_dir = entry.files[j];

					//found matched file...
					if(strcmp(curr_file_dir.fname, filename) == 0){
						//extenstion found...
						if(ext && ext[0]){
							//matching ext??
							if(strcmp(curr_file_dir.fext, ext) == 0){
								file = curr_file_dir;
								freeFileIndex = j;
								break;
							}
						}
						else{		//no extension
							if(strcmp(curr_file_dir.fext, "") == 0){
								file = curr_file_dir;
								freeFileIndex = j;
								break;
							}
						}
 					}
				}

				if(strcmp(file.fname, "") != 0){
					//if offset is beyond the file size...return error
					if(offset > file.fsize){
						return -EFBIG;
					}

					int bufferSize = strlen(buf);
					int file_block = 0;
					int offset_of_block = 0;
					int write_bytes_until_append = file.fsize - offset;

					if(offset != 0){
						file_block = offset/BLOCK_SIZE;
						offset_of_block = offset - file_block*BLOCK_SIZE;		//calculate # of blocks to get normal offset ==> needed for writing
					}

					//figure out correct block space now in fat
					int current_block = file.nStartBlock;
					if(file_block != 0){
						while(file_block > 0){
							current_block = fat.table[current_block];
							file_block--;
						}
					}

					int bytes_remaining = bufferSize;		//amount of bytes remaining

					FILE* disk = fopen(".disk", "r+b");
					fseek(disk, BLOCK_SIZE*current_block + offset_of_block, SEEK_SET);
					if(bufferSize >= BLOCK_SIZE){
						fwrite(buf, BLOCK_SIZE - offset_of_block, 1, disk);
						bytes_remaining -= (BLOCK_SIZE - offset_of_block);
						if(offset == size){
							file.fsize = offset + 1;
						}
					}
					else{
						fwrite(buf, bufferSize, 1, disk);
						char blank_arr[BLOCK_SIZE - bufferSize];
						int x = 0;
						for(x = 0; x < BLOCK_SIZE - bufferSize; x++){
							blank_arr[x] = '\0';
						}
						fwrite(blank_arr, BLOCK_SIZE - bufferSize, 1, disk);
						bytes_remaining -= bufferSize;

						if(file.fsize > size){
							if(offset == size){
								file.fsize = offset + 1;
							}
							else{
								file.fsize = size;
							}
						}
					}

						int byte_to_clear = size - bufferSize;

						while(bytes_remaining > 0){
							if(fat.table[current_block] == EOF){
								int free_block_is_found = 0;
								int k = 2;
								for(k = 2; k < MAX_FAT_ENTRIES_IN_BLOCK; k++){
									if(fat.table[k] == 0){
										fat.table[current_block] = k;
										fat.table[k] = EOF;
										current_block = k;
										free_block_is_found = 1;
										break;
									}
								}

								if(!free_block_is_found){
									return -EPERM;
								}
							}
							else{
								current_block = fat.table[current_block];
							}

							fseek(disk, BLOCK_SIZE*current_block, SEEK_SET);
							if(bytes_remaining >= BLOCK_SIZE){
								char* new_buffer = buf + (bufferSize - bytes_remaining);
								fwrite(new_buffer, BLOCK_SIZE, 1, disk);
								bytes_remaining -= BLOCK_SIZE;
							}
							else{
								char* new_buffer = buf + (bufferSize - bytes_remaining);
								fwrite(new_buffer, bytes_remaining, 1, disk);
								bytes_remaining = 0;
							}
						}

						int write_bytes = bufferSize - bytes_remaining;
						int appended_bytes = write_bytes - write_bytes_until_append;
						if(appended_bytes > 0){
							file.fsize += appended_bytes;
						}

						entry.files[freeFileIndex] = file;
						fseek(disk, dir.nStartBlock*BLOCK_SIZE, SEEK_SET);
						fwrite(&entry, BLOCK_SIZE, 1, disk);
						fclose(disk);
						writeToRoot(&root);
						writeToFAT(&fat);

						size = bufferSize;

			}else{
				return -EPERM;
			}
		}else{
				return -EPERM;
			}

		}else{
				if(strcmp(directory, "") == 0){
					return 0;
				}
				else if(strcmp(filename, "") == 0){
					return -EPERM;
				}
			}
	}

	return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/*
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
