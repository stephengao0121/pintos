#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"

#define MAX_BUFFER_CACHE_SIZE 64
#define DATA_SIZE 512

struct buffer_cache_entry{
	bool valid;					/*valid bit*/
	bool dirty;					/*dirty bit*/
	bool referenced;			/*referenced bit, used for eviction*/
	block_sector_t sector_id;	/*record the sector id it belongs to*/
	uint8_t data[DATA_SIZE];
};

void buffer_cache_init(void);
void buffer_cache_exit(void);

//write to buffer from source at sector_id
void buffer_cache_write(block_sector_t sector_id, void *source);
//read from buffer at sector_id and write to target
void buffer_cache_read(block_sector_t sector_id, void *target);

#endif