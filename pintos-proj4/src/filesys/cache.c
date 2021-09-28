#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "threads/synch.h"

//buffer cache
static struct buffer_cache_entry buffer_cache[MAX_BUFFER_CACHE_SIZE];
static struct lock buffer_cache_lock;

//flush the buffer cache entry to the disk
static void buffer_cache_flush_to_disk(struct buffer_cache_entry *entry);
//return the empty entry if there's any, if the buffer is full, evict some entry and return the empty entry
static struct buffer_cache_entry* buffer_cache_evict(void);
//look for the buffer cache entry using the sector id
static struct buffer_cache_entry* buffer_cache_lookup(block_sector_t sector_id);

void buffer_cache_init(void){
	for (int i = 0; i < MAX_BUFFER_CACHE_SIZE; i++)
		buffer_cache[i].valid = true;
	lock_init(&buffer_cache_lock);
}

void buffer_cache_exit(void){
	lock_acquire(&buffer_cache_lock);
	for(int i = 0; i < MAX_BUFFER_CACHE_SIZE; i++){
		if(buffer_cache[i].valid == false) 
			buffer_cache_flush_to_disk(&buffer_cache[i]);
	}
	lock_release(&buffer_cache_lock);
}

void buffer_cache_write(block_sector_t sector_id, void *source){
	lock_acquire(&buffer_cache_lock);
	struct buffer_cache_entry *entry;
	entry = buffer_cache_lookup(sector_id);
	//if cache miss, use eviction to find an empty entry to write to
	if(entry == NULL){
		entry = buffer_cache_evict();
		entry->sector_id = sector_id;
		entry->valid = false;
		entry->dirty = false;
		block_read(fs_device, sector_id, entry->data);
	}
	entry->dirty = true; 
	entry->referenced = true;
	memcpy(entry->data, source, DATA_SIZE);
	lock_release(&buffer_cache_lock);
}

void buffer_cache_read(block_sector_t sector_id, void *target){
	lock_acquire(&buffer_cache_lock);
	struct buffer_cache_entry *entry;
	entry = buffer_cache_lookup(sector_id);
	//if cache miss, use eviction to find an empty entry to write to
	if(entry == NULL){
		entry = buffer_cache_evict();
		entry->sector_id = sector_id;
		entry->valid = false;
		entry->dirty = false;
		block_read(fs_device, sector_id, entry->data);
	}
	entry->referenced = true;
	memcpy(target, entry->data, DATA_SIZE);
	lock_release(&buffer_cache_lock);
}

static void buffer_cache_flush_to_disk(struct buffer_cache_entry *entry){
	if(entry->dirty == true){
		block_write(fs_device, entry->sector_id, entry->data);
	}
}

static struct buffer_cache_entry* buffer_cache_evict(void){
	int clock_hand = 0;
	while(true){
		//return the empty entry if there's any
		if (buffer_cache[clock_hand].valid) return &buffer_cache[clock_hand];
		//if referenced before, set it to false; else, we have found the entry to be evicted
		if (buffer_cache[clock_hand].referenced) buffer_cache[clock_hand].referenced = false;
		else break;
		//increment the clock hand
		if (clock_hand == MAX_BUFFER_CACHE_SIZE) clock_hand = 1;
		else clock_hand ++;
	}
	if (buffer_cache[clock_hand].dirty) buffer_cache_flush_to_disk(&buffer_cache[clock_hand]);
	buffer_cache[clock_hand].valid = true;
	return &buffer_cache[clock_hand];
}

static struct buffer_cache_entry* buffer_cache_lookup(block_sector_t sector_id){
	for (int i = 0; i < MAX_BUFFER_CACHE_SIZE; i++){
		if (buffer_cache[i].valid) continue;
		//cache hit
		if (buffer_cache[i].sector_id == sector_id) return &buffer_cache[i];
	}
	//cache miss
	return NULL;
}