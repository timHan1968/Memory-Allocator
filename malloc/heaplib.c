#include "heaplib.h"


typedef struct _heap_header {
  unsigned int heap_size;
  void* start;
  unsigned int end;
} heap_header;

/* Useful shorthand: casts a pointer to a (char *) before adding */
#define ADD_BYTES(base_addr, num_bytes) (((char *)(base_addr)) + (num_bytes))

void print_debug_init(heap_header *header, int blocks_size){
//  #ifdef PRINT_DEBUG
  printf("heap starts at addr %p\n"   // C printing trick.
           "heap_size = %d\n"
           "start = %p\n"
           "end_idx = %d\n"
           "blocks_size = %d\n",          // Notice: no commas between line
           header, header->heap_size, header->start, header-> end, blocks_size);
//  #endif
}

void print_blocks(int *blocks, unsigned int end) {
  int i = 0; int counter = 1;
  while (i <= end) {
    int s = blocks[i] - (blocks[i] % 2);
    printf("Head %d: %d\n", counter, blocks[i]);
    printf("Tail %d: %d\n", counter, blocks[i+1+s]);
    i += 2+s; counter += 1;
  }
}

/* Helper function for resize. Works for overlapping as long as [des]
 * is before [src]. */
void memcopy(char *des, char *src, unsigned int size) {
  while (size > 0) {
    *des++ = *src++;
    size--;
  }
}

void hl_init(void *heap, unsigned int heap_size) {
    spin_lock(malloc_lock);
    int heap_r = (unsigned int) heap % 4;
    if (heap_r != 0) {heap += 4 - heap_r;}

    heap_header *header = (heap_header *)heap;
    header->heap_size = heap_size;
    unsigned int offset_addr = (unsigned int) ADD_BYTES(header, sizeof(heap_header));
    int after_mod = ((offset_addr+4) % 8);
    if (after_mod != 0){
      offset_addr += (8-after_mod);
    }
    header-> start = (int *) offset_addr;
    int* blocks = (int *) header-> start;
    int blocks_size = ((unsigned int) header + heap_size - (unsigned int) header-> start)/4;
    blocks_size = blocks_size-(blocks_size % 2);
    header -> end = blocks_size-1;

    //in use bit: 0 means free block, 1 means block in use
    blocks[0] = blocks_size - 2; //head
    blocks[blocks_size-1] = blocks_size - 2; //tail

    //print_debug_init(header, blocks_size);
    spin_unlock(malloc_lock);

}

void *hl_alloc(void *heap, unsigned int block_size) {
    spin_lock(malloc_lock);
    int heap_r = (unsigned int) heap % 4;
    if (heap_r != 0) {heap += 4 - heap_r;}

    heap_header *header = (heap_header *)heap;
    int end_idx = header -> end;
    int* blocks = (int *) header-> start;

    int target_size = block_size + 8; //Head and Tail tag
    unsigned int r = target_size % 8;
    if (r != 0) {target_size += (8-r);}
    target_size = target_size/4; //Bytes => Words

    void *return_ptr = ADD_BYTES(header->start, 4);//4 for the head tag
    int frag_s = header->heap_size;
    int return_idx = 0; bool split;
    int blk_s = 0; void *best_ptr = return_ptr;
    int i = 0;

    while (i < end_idx) {

      int use_bit = blocks[i] % 2;
      unsigned int s = blocks[i] - use_bit; //Block size (in use or not)

      if (use_bit == 1 ||
        (s < target_size && s != target_size - 2)) {
        i += (s + 2);
        return_ptr = ADD_BYTES(return_ptr, s *4 +8);
      //printf ("Current block not suitable.\n");
      } else {
        if (s == target_size - 2) {
          //Best scenario, no need to continue search
          frag_s = 0; blk_s = s;
          return_idx = i; best_ptr = return_ptr;
          split = false;
          break;
        } else {
          int leftover = s - target_size;
          if (leftover < frag_s) {
            frag_s = leftover; blk_s = s;
            return_idx = i; best_ptr = return_ptr;
            split = true;
          }
          i += (s + 2);
          return_ptr = ADD_BYTES(return_ptr, s *4 +8);
        }
      }
    }
    if (frag_s != (header->heap_size)) {
      if (split) {
        blocks[return_idx] = target_size -2 +1;
        blocks[return_idx+1+target_size-2] = target_size -2 + 1;
        blocks[return_idx+1+target_size-2+1] = blk_s - target_size;//Could be 0
        blocks[return_idx+1+blk_s] = blk_s - target_size;
        //printf("Block suitable but requires splitting\n");
      } else {
        //printf("Block suitable, no splitting\n");
        blocks[return_idx] += 1;
        blocks[return_idx+1+blk_s] += 1;
      }
      //printf ("The block pointer is %p\n", best_ptr);
      //printf("hl_alloc succeeds:\n");
      //print_blocks(blocks, end_idx);
      spin_unlock(malloc_lock);
      return best_ptr;
    } else {
      //printf ("Cannot allocate, not enough space.\n");
      //printf("hl_alloc fails, not enough space:\n");
      //print_blocks(blocks, end_idx);
      spin_unlock(malloc_lock);
      return NULL;
    }
}

void hl_release(void *heap, void *block) {
  spin_lock(malloc_lock);
  int heap_r = (unsigned int) heap % 4;
  if (heap_r != 0) {heap += 4 - heap_r;}

  //If Block is NULL, act like Nop
  if (block == NULL) {
    spin_unlock(malloc_lock);
    return;
  }
  //Initialize indexes for target block
  heap_header *header = (heap_header *)heap;
  int *blocks = (int *) header->start;
  int end_idx = header -> end;
  int blk_idx = ((unsigned int) block - (unsigned int) blocks)/4;
  //Checking previous and post block
  int s = blocks[blk_idx-1] - 1;
  int pre_tl = blk_idx-2; int pos_hd = blk_idx+s+1;
  bool pre_free = ((pre_tl > 0) && ((blocks[pre_tl] % 2) == 0));
  bool pos_free = ((pos_hd < end_idx) && ((blocks[pos_hd] % 2) == 0));
  //Four cases of merging
  if (!(pre_free || pos_free)) {
    //printf("No merging for this release.\n");
    blocks[blk_idx-1] -= 1;
    blocks[blk_idx+s] -= 1;
  } else if (pre_free && (! pos_free)) {
    //printf("Merging with previous block.\n");
    int pre_s = blocks[pre_tl];
    blocks[pre_tl-pre_s-1] = pre_s + s + 2;
    blocks[blk_idx+s] = pre_s + s + 2;
  } else if ((! pre_free) && pos_free) {
    //printf("Merging with post block.\n");
    int pos_s = blocks[pos_hd];
    blocks[blk_idx-1] = pos_s + s + 2;
    blocks[pos_hd+1+pos_s] = pos_s + s + 2;
  } else {
    //printf("Merging with both sides.\n");
    int pre_s = blocks[pre_tl];
    int pos_s = blocks[pos_hd];
    blocks[pre_tl-pre_s-1] = s + pre_s + pos_s + 4;
    blocks[pos_hd+1+pos_s] = s + pre_s + pos_s + 4;
  }
  //print_blocks(blocks, end_idx);
  spin_unlock(malloc_lock);
}

/*No lock version */
void *alloc(void *heap, unsigned int block_size) {
    int heap_r = (unsigned int) heap % 4;
    if (heap_r != 0) {heap += 4 - heap_r;}

    heap_header *header = (heap_header *)heap;
    int end_idx = header -> end;
    int* blocks = (int *) header-> start;

    int target_size = block_size + 8; //Head and Tail tag
    unsigned int r = target_size % 8;
    if (r != 0) {target_size += (8-r);}
    target_size = target_size/4; //Bytes => Words

    void *return_ptr = ADD_BYTES(header->start, 4);//4 for the head tag
    int frag_s = header->heap_size;
    int return_idx = 0; bool split;
    int blk_s = 0; void *best_ptr = return_ptr;
    int i = 0;

    while (i < end_idx) {

      int use_bit = blocks[i] % 2;
      unsigned int s = blocks[i] - use_bit; //Block size (in use or not)

      if (use_bit == 1 ||
        (s < target_size && s != target_size - 2)) {
        i += (s + 2);
        return_ptr = ADD_BYTES(return_ptr, s *4 +8);
      //printf ("Current block not suitable.\n");
      } else {
        if (s == target_size - 2) {
          //Best scenario, no need to continue search
          frag_s = 0; blk_s = s;
          return_idx = i; best_ptr = return_ptr;
          split = false;
          break;
        } else {
          int leftover = s - target_size;
          if (leftover < frag_s) {
            frag_s = leftover; blk_s = s;
            return_idx = i; best_ptr = return_ptr;
            split = true;
          }
          i += (s + 2);
          return_ptr = ADD_BYTES(return_ptr, s *4 +8);
        }
      }
    }
    if (frag_s != (header->heap_size)) {
      if (split) {
        blocks[return_idx] = target_size -2 +1;
        blocks[return_idx+1+target_size-2] = target_size -2 + 1;
        blocks[return_idx+1+target_size-2+1] = blk_s - target_size;//Could be 0
        blocks[return_idx+1+blk_s] = blk_s - target_size;
        //printf("Block suitable but requires splitting\n");
      } else {
        //printf("Block suitable, no splitting\n");
        blocks[return_idx] += 1;
        blocks[return_idx+1+blk_s] += 1;
      }
      //printf ("The block pointer is %p\n", best_ptr);
      //printf("Resize's alloc succeeds: \n");
      //print_blocks(blocks, end_idx);
      return best_ptr;
    } else {
      //printf ("Resize's alloc failed, not enough space.\n");
      //print_blocks(blocks, end_idx);
      return NULL;
    }
}

/* No lock version */
void release(void *heap, void *block) {
  int heap_r = (unsigned int) heap % 4;
  if (heap_r != 0) {heap += 4 - heap_r;}
  //If Block is NULL, act like Nop
  if (block == NULL) {
    return;
  }
  //Initialize indexes for target block
  heap_header *header = (heap_header *)heap;
  int *blocks = (int *) header->start;
  int end_idx = header -> end;
  int blk_idx = ((unsigned int) block - (unsigned int) blocks)/4;
  //Checking previous and post block
  int s = blocks[blk_idx-1] - 1;
  int pre_tl = blk_idx-2; int pos_hd = blk_idx+s+1;
  bool pre_free = ((pre_tl > 0) && ((blocks[pre_tl] % 2) == 0));
  bool pos_free = ((pos_hd < end_idx) && ((blocks[pos_hd] % 2) == 0));
  //Four cases of merging
  if (!(pre_free || pos_free)) {
    //printf("No merging for this release.\n");
    blocks[blk_idx-1] -= 1;
    blocks[blk_idx+s] -= 1;
  } else if (pre_free && (! pos_free)) {
    //printf("Merging with previous block.\n");
    int pre_s = blocks[pre_tl];
    blocks[pre_tl-pre_s-1] = pre_s + s + 2;
    blocks[blk_idx+s] = pre_s + s + 2;
  } else if ((! pre_free) && pos_free) {
    //printf("Merging with post block.\n");
    int pos_s = blocks[pos_hd];
    blocks[blk_idx-1] = pos_s + s + 2;
    blocks[pos_hd+1+pos_s] = pos_s + s + 2;
  } else {
    //printf("Merging with both sides.\n");
    int pre_s = blocks[pre_tl];
    int pos_s = blocks[pos_hd];
    blocks[pre_tl-pre_s-1] = s + pre_s + pos_s + 4;
    blocks[pos_hd+1+pos_s] = s + pre_s + pos_s + 4;
  }
  //print_blocks(blocks, end_idx);
}

/* Helper function that check if enough free blocks for a resize
   around the original block WITHOUT changing any tags. */
bool check_release(void *heap, void *block, unsigned int new_size) {
  //Initialize array and indexes
  heap_header *header = (heap_header *)heap;
  int *blocks = (int *) header->start;
  int end_idx = header -> end;
  int blk_idx = ((unsigned int) block - (unsigned int) blocks)/4;
  //Calculate the target size in words
  int target_size = new_size + 8;
  unsigned int r = target_size % 8;
  if (r != 0) {target_size += (8-r);}
  target_size = target_size/4;
  //Check for available sizes
  int s = blocks[blk_idx-1] - 1;
  int available = s;
  int pre = blk_idx-2; int pos = blk_idx + s +1;
  if (pre>=0 && (blocks[pre] % 2) == 0) {
    available += blocks[pre] + 2;
  }
  if (pos<=end_idx && (blocks[pos] % 2) == 0) {
    available += blocks[pos] + 2;
  }
  return (available >= target_size || available == (target_size-2));
}


void *hl_resize(void *heap, void *block, unsigned int new_size) {
  int heap_r = (unsigned int) heap % 4;
  if (heap_r != 0) {heap += 4 - heap_r;}

  spin_lock(malloc_lock);
  //Null block equivalent to hl_alloc
  if (block == NULL) {
    //printf("Null block pointer, calling alloc.\n");
    spin_unlock(malloc_lock);
    return alloc(heap, new_size);
  }
  //Check if pre_release is safe or not
  bool pre_release = check_release(heap, block, new_size);

  if (pre_release) {release(heap, block);}
  //Try if alloc can find resize block with/without releasing first
  char *des = alloc(heap, new_size);
  if (des == NULL) {
    //printf("Resize too large, cannot fullfill request.\n");
    spin_unlock(malloc_lock);
    return NULL;
  } else {
    if (!pre_release){release(heap, block);} //Release if has not yet
    //Destination block index and size
    int *des_hd = (int *)((unsigned int) des - 4);
    int des_s = des_hd[0] - (des_hd[0] %2);
    //Source block index and size
    char *src = block;
    int *src_hd = (int *)((unsigned int) src - 4);
    int src_s = src_hd[0] - (src_hd[0] % 2);
    //Compare and decide how much to copy
    if (src == des) {
      spin_unlock(malloc_lock);
      //printf("Resize possible: same pointer, no need to copy.\n");
      return des;
    }
    if (des_s < src_s) {
      //printf("Resize possible: new block smaller, limited copy required.\n");
      memcopy(des, src, des_s*4);
      spin_unlock(malloc_lock);
      return des;
    } else {
      //printf("Resize possible: new block bigger, full copy required.\n");
      memcopy(des, src, src_s*4);
      spin_unlock(malloc_lock);
      return des;
    }
  }
}
