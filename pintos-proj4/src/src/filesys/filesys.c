#include "filesys/filesys.h"
#include "filesys/cache.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  buffer_cache_init();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  buffer_cache_exit();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  block_sector_t inode_sector = 0;
  //struct dir *dir = dir_open_root ();
  char directory[strlen(name)];
  char filename[strlen(name)];
  split_path(name, directory, filename);
  struct dir *dir = dir_open_path(directory);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, filename, inode_sector, is_dir));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  //struct dir *dir = dir_open_root ();
  if(strlen(name) == 0) return NULL;
  struct inode *inode = NULL;
  char directory[strlen(name)];
  char filename[strlen(name)];
  split_path(name, directory, filename);
  struct dir *dir = dir_open_path(directory);

  if(dir == NULL) return NULL;
  if (strlen(filename) > 0) {
      //dir_lookup (dir, name, &inode);
      dir_lookup(dir, filename, &inode);
      dir_close(dir);
  }
  else{ /* empty file name.*/
      inode = dir_get_inode(dir);
      if (inode == NULL || inode_is_removed(inode)) return NULL;
  }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  //struct dir *dir = dir_open_root ();
  //bool success = dir != NULL && dir_remove (dir, name);
  char directory[strlen(name)];
  char filename[strlen(name)];
  split_path(name, directory, filename);
  struct dir *dir = dir_open_path(directory);
  bool success = (dir != NULL && dir_remove (dir, filename));
  dir_close (dir); 

  return success;
}

/* Changes the directory of current process to DIR. If success,
   return true; else, return false.*/
bool
filesys_chdir(const char *dir)
{
    struct dir *new_dir = dir_open_path(dir);
    struct thread *t = thread_current();
    if (new_dir == NULL) return false;
    dir_close(t->current_working_dir);
    t->current_working_dir = new_dir;
    return true;
}

/* Create a new directory DIR. If success, or already exists,
   return true; else, return false;*/
bool
filesys_mkdir(const char *dir)
{
    return filesys_create(dir, 0, true);
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
