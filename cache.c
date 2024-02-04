#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"


//Uncomment the below code before implementing cache functions.
static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  if (cache != NULL) {
    return -1;
  } 
  if (num_entries > 4096 || num_entries < 2) {
    return -1;
  }

  int entry_size = sizeof(cache_entry_t);

  cache = calloc(num_entries, entry_size);

  if (cache == NULL) {
    return -1; // Allocation failed
  }

  for (int i = 0; i < num_entries; ++i) {
    cache[i] = (cache_entry_t) { .valid = false, .disk_num = -1, .block_num = -1, .clock_accesses = 0 };
  }

  cache_size = num_entries;

  return 1;
}

int cache_destroy(void) {
  if (cache == NULL) {
  return -1;
  }
  
  free(cache);
  cache = NULL;
  cache_size =0;
  return 1;

}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {

  

  if ( cache == NULL || buf ==  NULL || disk_num < 0 || disk_num > 15 || block_num <0 || block_num > 255  ) {
    
    return -1; 
  }

  
  
  num_queries ++;


  for ( int i = 0; i< cache_size ; i++) {

    //printf("cache[%d] = %p\n", i, (void *)&cache[i]);

    //if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true){
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid == true){

      //printf("is it breaking here");
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      num_hits++;
      clock++;
      cache[i].clock_accesses = clock;
    
      return 1;

    }  
    
  } 
  return -1;
} 




void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  if (cache == NULL || disk_num < 0 || disk_num > 15 || block_num < 0 || block_num > 255 || buf == NULL) {
    return;
  }

  int i = 0;
  while (i < cache_size && !(cache[i].disk_num == disk_num && cache[i].block_num == block_num)) {
    i++;
  }

  if (i < cache_size) {
    memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
    clock++;
    cache[i].clock_accesses = clock;
  }
}



int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if (cache == NULL || buf == NULL || disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK) {
    return -1;
  }

  int index = 0;
  int msu = clock;
  int i = 0;

  while (i < cache_size) {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
      return -1; // Cache is already there
    }

    if (cache[i].valid == false) {
      
      cache[i].valid = true;
      index = i;
      break;
    }

    // Used the most recent cache for the insert
    index = (cache[i].clock_accesses == msu) ? i : index;
    i++;
  }

  clock++;
  cache[index].clock_accesses = clock;
  cache[index].block_num = block_num;
  cache[index].disk_num = disk_num;
  memcpy(cache[index].block, buf, JBOD_BLOCK_SIZE);

  return 1;
}

bool cache_enabled(void) {
  return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
} 


int cache_resize(int new_num_entries) {
  if (cache == NULL) {
    return -1;
  }

  int entry_size = sizeof(cache_entry_t);
  cache = realloc(cache, new_num_entries * entry_size);
  cache_size = new_num_entries;

  int index = 0;
  while (index < cache_size) {
    if (cache[index].valid != true) {
      cache[index].disk_num = -1;
      cache[index].block_num = -1;
      cache[index].clock_accesses = 0;
    }
    index++;
  }

  return 1;
}