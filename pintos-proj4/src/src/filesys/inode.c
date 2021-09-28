#include "filesys/inode.h"
#include "filesys/cache.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECR_BLOCKS_NUM 123
#define INDIRECT_BLOCKS_PER_SECTOR 128
#define DOUBLY_INDIRECT_BLOCKS_PER_SECTOR 128*128

static size_t min(size_t a, size_t b){
  return a < b ? a : b;
}

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;                             /* First data sector. */
    //uint32_t unused[125];                             /* Not used. */
    block_sector_t direct_blocks[DIRECR_BLOCKS_NUM];    /*123 direct blocks in each sector*/
    block_sector_t indirect_block;                      /*1 indirect block in each sector*/
    block_sector_t doubly_indirect_block;               /*1 doubly indirect block in each sector*/
    off_t length;                                       /* File size in bytes. */
    unsigned magic;                                     /* Magic number. */
    bool is_dir;
  };

struct inode_indirect_block_sector{
  block_sector_t indirect_blocks[INDIRECT_BLOCKS_PER_SECTOR];
};

static char zeros[BLOCK_SECTOR_SIZE];
static bool inode_extend(struct inode_disk *inode_disk, off_t length);
static bool inode_extend_indirect(block_sector_t *block, size_t num_sectors, int level);
static bool inode_free(struct inode *inode, off_t length);
static void inode_free_indirect(block_sector_t block, size_t num_sectors, int level);
static block_sector_t index_to_sector(const struct inode_disk *inode_disk, off_t index);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  //if (pos < inode->data.length)
    //return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  if(0 <= pos && pos < inode->data.length){
    off_t index = pos / BLOCK_SECTOR_SIZE;
    return index_to_sector(&inode->data, index);
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      //size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;
      //if (free_map_allocate (sectors, &disk_inode->start)) 
      if(inode_extend(disk_inode, disk_inode->length))
        {
          //buffer_write (fs_device, sector, disk_inode);
          buffer_cache_write(sector, disk_inode);
          /*if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
                //buffer_cache_write(disk_inode->start + i, zeros);
            }*/
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //block_read (fs_device, inode->sector, &inode->data);
  buffer_cache_read(inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          /*free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); */
          inode_free(inode, inode->data.length);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  //read beyond EOF returns no byte
  if(byte_to_sector(inode, offset + size - 1) == -1)
    return 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          //block_read (fs_device, sector_idx, buffer + bytes_read);
          buffer_cache_read(sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          //block_read (fs_device, sector_idx, bounce);
          buffer_cache_read(sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  //needs to extend if write after EOF
  if(byte_to_sector(inode, offset + size - 1) == -1){
    //printf("extended !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    bool success;
    success = inode_extend(&inode->data, offset + size);
    if(!success) return 0;
    inode->data.length = offset + size;
    buffer_cache_write(inode->sector, &inode->data);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          //block_write (fs_device, sector_idx, buffer + bytes_written);
          buffer_cache_write(sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            //block_read (fs_device, sector_idx, bounce);
            buffer_cache_read(sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          //block_write (fs_device, sector_idx, bounce);
          buffer_cache_write(sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

//adjustment to byte_to_sector, in order to fit the multilevel structure
static block_sector_t index_to_sector(const struct inode_disk *inode_d, off_t index){
  int index_base = 0, index_bound = 0;

  //if the index is within the direct blocks
  index_bound += DIRECR_BLOCKS_NUM;
  if(index < index_bound) return inode_d->direct_blocks[index];
  index_base = index_bound;

  //if the index is within the indirect block
  index_bound += INDIRECT_BLOCKS_PER_SECTOR;
  if(index < index_bound){
    struct inode_indirect_block_sector *indirect_inode_disk;
    block_sector_t block;
    indirect_inode_disk = calloc(1, sizeof(struct inode_indirect_block_sector));
    buffer_cache_read(inode_d->indirect_block, indirect_inode_disk);
    block = indirect_inode_disk->indirect_blocks[index - index_base];
    free(indirect_inode_disk);
    return block;
  }
  index_base = index_bound;

  //if the index is within the doubly indirect indirect block
  index_bound += DOUBLY_INDIRECT_BLOCKS_PER_SECTOR;
  if(index < index_bound){
    struct inode_indirect_block_sector *indirect_inode_disk;
    block_sector_t block;
    off_t first_level_index = (index - index_base) / INDIRECT_BLOCKS_PER_SECTOR;
    off_t second_level_index = (index - index_base) % INDIRECT_BLOCKS_PER_SECTOR;

    indirect_inode_disk = calloc(1, sizeof(struct inode_indirect_block_sector));
    buffer_cache_read(inode_d->doubly_indirect_block, indirect_inode_disk);
    buffer_cache_read(indirect_inode_disk->indirect_blocks[first_level_index], indirect_inode_disk);
    block = indirect_inode_disk->indirect_blocks[second_level_index];
    free(indirect_inode_disk);
    return block;
  }

  return -1;
}

//extend the inode blocks to make the file hold at least length bytes
static bool inode_extend(struct inode_disk *inode_d, off_t length){
  if(length < 0) return false;

  size_t num_sectors, num;
  num_sectors = bytes_to_sectors(length);

  //direct blocks
  num = min(num_sectors, DIRECR_BLOCKS_NUM);
  for (int i = 0; i < num; i++){
    if(inode_d->direct_blocks[i] == 0){
      if(!free_map_allocate(1, &inode_d->direct_blocks[i])) return false;
      buffer_cache_write(inode_d->direct_blocks[i], zeros);
    }
  }
  num_sectors -= num;
  if(num_sectors == 0) return true;

  //indirect block
  num = min(num_sectors, INDIRECT_BLOCKS_PER_SECTOR);
  if(!inode_extend_indirect(&inode_d->indirect_block, num, 1)) return false;
  num_sectors -= num;
  if(num_sectors == 0) return true;

  //doubly indirect block
  num = min(num_sectors, DOUBLY_INDIRECT_BLOCKS_PER_SECTOR);
  if(!inode_extend_indirect(&inode_d->doubly_indirect_block, num, 2)) return false;
  num_sectors -= num;
  if(num_sectors == 0) return true;

  return false;
}

//extend indirect blocks, used in inode_extend
static bool inode_extend_indirect(block_sector_t *block, size_t num_sectors, int level){
  struct inode_indirect_block_sector indirect_block;
  size_t unit;
  int bound;
  if (level == 1) unit = 1;
  else unit = INDIRECT_BLOCKS_PER_SECTOR;

  if(level == 0){
    if(*block == 0){
      if(!free_map_allocate(1, block)) return false;
      buffer_cache_write(*block, zeros);
    }
    return true;
  }

  if(*block == 0){
    free_map_allocate(1, block);
    buffer_cache_write(*block, zeros);
  }
  buffer_cache_read(*block, &indirect_block);
  bound = DIV_ROUND_UP(num_sectors, unit);
  for(int i = 0; i < bound; i++){
    int num = min(num_sectors, unit);
    if(!inode_extend_indirect(&indirect_block.indirect_blocks[i], num, level - 1)) return false;
    num_sectors -= num;
  }

  buffer_cache_write(*block, &indirect_block);
  return true;
}

//free the inode blocks
static bool inode_free(struct inode *inode, off_t length){
  if(length < 0) return false;
  size_t num_sectors, num;
  num_sectors = bytes_to_sectors(length);

  //direct blocks
  num = min(num_sectors, DIRECR_BLOCKS_NUM);
  for(int i = 0; i < num; i++)
    free_map_release(inode->data.direct_blocks[i], 1);
  num_sectors -= num;

  //indirect block
  num = min(num_sectors, INDIRECT_BLOCKS_PER_SECTOR);
  if(num > 0){
    inode_free_indirect(inode->data.indirect_block, num, 1);
    num_sectors -= num;
  }

  //doubly indirect block
  num = min(num_sectors, DOUBLY_INDIRECT_BLOCKS_PER_SECTOR);
  if(num > 0){
    inode_free_indirect(inode->data.doubly_indirect_block, num, 2);
    num_sectors -= num;
  }
  return true;
}

//free the indirect block, used in inode_free
static void inode_free_indirect(block_sector_t block, size_t num_sectors, int level){
  size_t unit, num;
  struct inode_indirect_block_sector indirect_block;
  buffer_cache_read(block, &indirect_block);
  if(level == 1) unit = 1;
  else unit = INDIRECT_BLOCKS_PER_SECTOR;

  if(level == 0){
    free_map_release(block, 1);
    return;
  }

  for(int i = 0; i < DIV_ROUND_UP(num_sectors, unit); i++){
    num = min(num_sectors, unit);
    inode_free_indirect(indirect_block.indirect_blocks[i], num, level - 1);
    num_sectors -= num;
  }
  free_map_release(block, 1);
}

bool inode_is_removed(struct inode *inode){
  return inode->removed;
}

bool
inode_is_dir(struct inode *inode)
{
    return inode->data.is_dir;
}

block_sector_t
inode_get_id(struct inode *inode)
{
    return inode->sector;
}