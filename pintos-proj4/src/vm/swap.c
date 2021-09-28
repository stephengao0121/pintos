#include "vm/swap.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct block *swap_block;
/*a bit map that shows which blocks are avaliable*/
struct bitmap *swap_map;

static bool swap_map_allocate(block_sector_t *sector);

void swap_init(void){
	swap_block = block_get_role(BLOCK_SWAP);
	swap_map = bitmap_create(block_size(swap_map) / SECTORS_PER_PAGE);
}

block_sector_t swap_write(void *kernelPage){
	block_sector_t sector;
	if (swap_map_allocate(&sector))
		PANIC("no swap spave");

	for (int i = 0; i < SECTORS_PER_PAGE; i++, sector ++, kernelPage += BLOCK_SECTOR_SIZE)
		block_write(swap_block, sector, kernelPage);
	return sector - SECTORS_PER_PAGE;
}

void swap_read(block_sector_t sector, void *kernelPage){
	for (int i = 0; i < SECTORS_PER_PAGE; i++, sector++, kernelPage += BLOCK_SECTOR_SIZE)
		block_read(swap_block, sector, kernelPage);
	swap_release(sector - SECTORS_PER_PAGE);
}

void swap_release(block_sector_t sector){
	bitmap_set_multiple(swap_map, sector / SECTORS_PER_PAGE, 1, false);
}

static bool swap_map_allocate(block_sector_t *sector){
	block_sector_t sec = bitmap_scan_and_flip(swap_map, 0, 1, false);
	if(sec != BITMAP_ERROR) *sector = sec * SECTORS_PER_PAGE;
	return sec != BITMAP_ERROR;
}