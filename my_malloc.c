#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "my_malloc.h"
#include "printing.h"

/* 
 * Due to the way assert() prints error messges we use out own assert function
 * for deteminism when testing assertions
 */
#ifdef TEST_ASSERT
inline static void assert(int e) {
  if (!e) {
    fprintf(stderr, "Assertion Failed!\n");
    exit(1);
  }
}
#else
#include <assert.h>
#endif

/*
 * Mutex to ensure thread safety for the freelist
 */
static pthread_mutex_t mutex;

/*
 * Array of sentinel nodes for the freelists
 */
header freelistSentinels[N_LISTS];

/*
 * Pointer to the second fencepost in the most recently allocated chunk from
 * the OS. Used for coalescing chunks
 */
header * lastFencePost;

/*
 * Pointer to maintain the base of the heap to allow printing based on the
 * distance from the base of the heap
 */
void * base;

/*
 * List of chunks allocated by the OS for printing boundary tags
 */
header * osChunkList[MAX_OS_CHUNKS];
size_t numOsChunks = 0;

/*
 * direct the compiler to run the init function before running main
 * this allows initialization of required globals
 */
static void init(void) __attribute__((constructor));

// Helper functions for manipulating pointers to headers
static inline header * get_header_from_offset(void * ptr, ptrdiff_t off);
static inline header * get_left_header(header * h);
static inline header * ptr_to_header(void * p);

// Helper functions for allocating more memory from the OS
static inline void initialize_fencepost(header * fp, size_t left_size);
static inline void insert_os_chunk(header * hdr);
static inline void insert_fenceposts(void * raw_mem, size_t size);
static header * allocate_chunk(size_t size);

// Helper functions for freeing a block
static inline void deallocate_object(void * p);

// Helper functions for allocating a block
static inline header * allocate_object(size_t raw_size);

// Helper functions for verifying that the data structures are structurally valid
static inline header * detect_cycles();
static inline header * verify_pointers();
static inline bool verify_freelist();
static inline header * verify_chunk(header * chunk);
static inline bool verify_tags();

// Library initialization
static void init();

/**
 * @brief Helper function to retrieve a header pointer from a pointer and an
 *        offset
 *
 * @param ptr base pointer
 * @param off number of bytes from base pointer where header is located
 *
 * @return a pointer to a header offset bytes from pointer
 */
static inline header * get_header_from_offset(void * ptr, ptrdiff_t off) {
  return (header *)((char *)ptr + off);
}

/**
 * @brief Helper function to get the header to the right of a given header
 *
 * @param h original header
 *
 * @return header to the right of h
 */
header * get_right_header(header * h) {
  return get_header_from_offset(h, h->size);
}

/**
 * @brief Helper function to get the header to the left of a given header
 *
 * @param h original header
 *
 * @return header to the left of h
 */
inline static header * get_left_header(header * h) {
  return get_header_from_offset(h, -h->left_size);
}

/**
 * @brief Fenceposts are marked as always allocated and may need to have
 * a left object size to ensure coalescing happens properly
 *
 * @param fp a pointer to the header being used as a fencepost
 * @param left_size the size of the object to the left of the fencepost
 */
inline static void initialize_fencepost(header * fp, size_t left_size) {
  fp->allocated = FENCEPOST;
  fp->size = ALLOC_HEADER_SIZE;
  fp->left_size = left_size;
}

/**
 * @brief Helper function to maintain list of chunks from the OS for debugging
 *
 * @param hdr the first fencepost in the chunk allocated by the OS
 */
inline static void insert_os_chunk(header * hdr) {
  if (numOsChunks < MAX_OS_CHUNKS) {
    osChunkList[numOsChunks++] = hdr;
  }
}

/**
 * @brief Given a chunk of memory, insert fenceposts at the left and right
 * boundaries of the block to prevent coalescing outside of the block.
 *
 * @param raw_mem a void pointer to the memory chunk to initialize
 * @param size the size of the allocated chunk
 */
inline static void insert_fenceposts(void * raw_mem, size_t size) {
  char * mem = (char *)raw_mem;

  header * leftFencePost = (header *)mem;
  initialize_fencepost(leftFencePost, ALLOC_HEADER_SIZE);

  header * rightFencePost =
      get_header_from_offset(mem, size - ALLOC_HEADER_SIZE);
  initialize_fencepost(rightFencePost, size - 2 * ALLOC_HEADER_SIZE);
}

/**
 * @brief Allocate another chunk from the OS and prepare to insert it
 * into the free list
 *
 * @param size The size to allocate from the OS
 *
 * @return A pointer to the allocable block in the chunk (just after the
 * first fencepost)
 */
static header * allocate_chunk(size_t size) {
  void * mem = sbrk(size);
  // syscall fails
  if (mem == (void *)-1) {
    return NULL;
  }
  insert_fenceposts(mem, size);
  header * hdr = (header *)((char *)mem + ALLOC_HEADER_SIZE);
  hdr->allocated = UNALLOCATED;
  hdr->size = size - 2 * ALLOC_HEADER_SIZE;
  hdr->left_size = ALLOC_HEADER_SIZE;
  return hdr;
}

/**
 * @brief Return the ceiling of num as an integer.
 *
 * @param num the floating point value to ceil
 * @return the smallest integer >= num
 *
 * TODO: Implement without using the math.h ceil() function.
 * Hint: cast to int (truncates toward zero), then check if the truncated
 * value is less than num.
 */
inline static int my_ceil(float num) {
  // TODO
  (void)num;
  return 0;
}

/**
 * @brief Compute the total block size needed for a user allocation of
 *        raw_size bytes.
 *
 * @param raw_size number of user bytes requested
 * @return total block size (aligned to 8 bytes, at least sizeof(header))
 *
 * TODO: Implement this function.
 * Steps:
 *   1. Add ALLOC_HEADER_SIZE to raw_size to account for the header metadata.
 *   2. Round the result up to the nearest multiple of 8 using my_ceil().
 *   3. Return the larger of the rounded size and sizeof(header) — freed blocks
 *      must always have room for the next/prev free-list pointers.
 */
inline static unsigned calculate_actual_size(size_t raw_size) {
  // TODO
  (void)raw_size;
  return 0;
}

/**
 * @brief Helper to get the header from a pointer returned by my_malloc.
 *
 * @param p pointer to the data[] field of an allocated block
 * @return pointer to the block's header
 *
 * TODO: Implement this function.
 * Recall that the user pointer p points to the data[] field inside the header
 * struct. The header begins ALLOC_HEADER_SIZE bytes *before* p in memory.
 */
static inline header * ptr_to_header(void * p) {
  // TODO
  (void)p;
  return NULL;
}

/**
 * @brief Allocate a block satisfying a user request of raw_size bytes.
 *
 * @param raw_size number of bytes the user needs
 * @return pointer to the data[] field of the allocated block, or NULL
 *
 * TODO: Implement the core malloc algorithm.
 *
 * Steps:
 *   1. Return NULL for a zero or negative request.
 *
 *   2. Compute the aligned block size with calculate_actual_size().
 *
 *   3. Determine the starting free-list index:
 *        index = (requestSize / 8) - 1
 *      Clamp to [0, N_LISTS - 1].
 *
 *   4. Walk free lists from `index` up to N_LISTS - 1:
 *      For each block hdr in list i:
 *        a. Exact fit (hdr->size == requestSize):
 *             Mark ALLOCATED, unlink hdr, return &hdr->data[0].
 *        b. Too-small remainder (hdr->size - requestSize < sizeof(header)):
 *             Treat as exact fit (fragmentation is unavoidable here).
 *        c. Splittable (hdr->size - requestSize >= sizeof(header)):
 *             Carve requestSize bytes off the RIGHT end of hdr.
 *             Update hdr->size and the new block's size / left_size / allocated.
 *             Update the block to hdr's right so its left_size reflects hdr's
 *             new (smaller) size.
 *             Move hdr to the free list matching its new size class.
 *             Return a pointer to the new block's data[].
 *
 *   5. No block found — request a new chunk:
 *        newBlock = allocate_chunk(ARENA_SIZE)
 *        If NULL: set errno = ENOMEM, return NULL.
 *
 *        newFencePost = get_header_from_offset(newBlock, -ALLOC_HEADER_SIZE)
 *
 *        If the new chunk is ADJACENT to the old one
 *        (get_right_header(lastFencePost) == newFencePost):
 *
 *          Sub-case A — left neighbor of lastFencePost is ALLOCATED:
 *            Create a new free block that spans the two fenceposts and
 *            newBlock. Insert it into freelistSentinels[N_LISTS - 1].
 *            Update lastFencePost. Retry by calling allocate_object(raw_size).
 *
 *          Sub-case B — left neighbor of lastFencePost is UNALLOCATED:
 *            Extend that free block to absorb the two fenceposts and newBlock.
 *            Move it to the correct size-class list if its index changed.
 *            Update lastFencePost. Retry by calling allocate_object(raw_size).
 *
 *        If NOT adjacent:
 *          Register the new chunk: insert_os_chunk(newFencePost).
 *          Add newBlock to freelistSentinels[N_LISTS - 1].
 *          Retry by calling allocate_object(raw_size).
 *
 * Useful symbols: freelistSentinels[], lastFencePost, N_LISTS, ARENA_SIZE,
 *   ALLOC_HEADER_SIZE, get_header_from_offset(), get_right_header(),
 *   get_left_header(), insert_os_chunk().
 */
static inline header * allocate_object(size_t raw_size) {
  // TODO
  (void)raw_size;
  return NULL;
}

/**
 * @brief Deallocate a block previously returned by my_malloc.
 *
 * @param p pointer returned by a prior call to my_malloc (may be NULL)
 *
 * TODO: Implement the core free algorithm.
 *
 * Steps:
 *   1. If p is NULL, return immediately (free(NULL) is a no-op).
 *
 *   2. Recover the header: currBlock = ptr_to_header(p).
 *
 *   3. Double-free check: if currBlock->allocated == UNALLOCATED, print
 *      "Double Free Detected\n" and assert(false).
 *
 *   4. Mark currBlock UNALLOCATED.
 *
 *   5. Get neighbors:
 *        leftBlock  = get_left_header(currBlock)
 *        rightBlock = get_right_header(currBlock)
 *
 *      Compute origList (the size-class index currBlock WOULD go into before
 *      any coalescing, so you can detect when a block needs to move lists).
 *
 *   6. Coalesce according to neighbor states:
 *
 *      Case A — both neighbors UNALLOCATED:
 *        leftBlock->size += currBlock->size + rightBlock->size
 *        Update the block to the right of rightBlock: its left_size = leftBlock->size
 *        Unlink rightBlock from its free list.
 *        If leftBlock's new size class differs from origList, unlink leftBlock
 *        from its current list (it will be re-inserted in step 7).
 *        Set currBlock = leftBlock.
 *
 *      Case B — only right neighbor UNALLOCATED:
 *        currBlock->size += rightBlock->size
 *        Inherit rightBlock's list links (splice currBlock in place of
 *        rightBlock so the list stays intact).
 *        Update the block to the right of rightBlock: its left_size = currBlock->size
 *        If the size class changed, unlink and re-insert currBlock.
 *
 *      Case C — only left neighbor UNALLOCATED:
 *        leftBlock->size += currBlock->size
 *        Update get_right_header(currBlock)->left_size = leftBlock->size
 *        If leftBlock's new size class differs from origList, unlink leftBlock.
 *        Set currBlock = leftBlock.
 *
 *      Case D — neither neighbor UNALLOCATED:
 *        No merging needed; just fall through to step 7.
 *
 *   7. Compute the insertion index for currBlock:
 *        insertionIndex = ((currBlock->size - ALLOC_HEADER_SIZE) / 8) - 1
 *        Clamp to N_LISTS - 1.
 *      Insert currBlock at the front of freelistSentinels[insertionIndex].
 *
 * Useful symbols: freelistSentinels[], N_LISTS, ALLOC_HEADER_SIZE,
 *   ptr_to_header(), get_left_header(), get_right_header().
 */
static inline void deallocate_object(void * p) {
  // TODO
  (void)p;
}

/**
 * @brief Helper to detect cycles in the free list
 * https://en.wikipedia.org/wiki/Cycle_detection#Floyd's_Tortoise_and_Hare
 *
 * @return One of the nodes in the cycle or NULL if no cycle is present
 */
static inline header * detect_cycles() {
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    for (header *slow = freelist->next, *fast = freelist->next->next;
         fast != freelist; slow = slow->next, fast = fast->next->next) {
      if (slow == fast) {
        return slow;
      }
    }
  }
  return NULL;
}

/**
 * @brief Helper to verify that there are no unlinked previous or next pointers
 *        in the free list
 *
 * @return A node whose previous and next pointers are incorrect or NULL if no
 *         such node exists
 */
static inline header * verify_pointers() {
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    for (header * cur = freelist->next; cur != freelist; cur = cur->next) {
      if (cur->next->prev != cur || cur->prev->next != cur) {
        return cur;
      }
    }
  }
  return NULL;
}

/**
 * @brief Verify the structure of the free list is correct by checking for
 *        cycles and misdirected pointers
 *
 * @return true if the list is valid
 */
static inline bool verify_freelist() {
  header * cycle = detect_cycles();
  if (cycle != NULL) {
    fprintf(stderr, "Cycle Detected\n");
    print_sublist(print_object, cycle->next, cycle);
    return false;
  }

  header * invalid = verify_pointers();
  if (invalid != NULL) {
    fprintf(stderr, "Invalid pointers\n");
    print_object(invalid);
    return false;
  }

  return true;
}

/**
 * @brief Helper to verify that the sizes in a chunk from the OS are correct
 *        and that allocated node's canary values are correct
 *
 * @param chunk ARENA_SIZE chunk allocated from the OS
 *
 * @return a pointer to an invalid header or NULL if all headers are valid
 */
static inline header * verify_chunk(header * chunk) {
  if (chunk->allocated != FENCEPOST) {
    fprintf(stderr, "Invalid fencepost\n");
    print_object(chunk);
    return chunk;
  }

  for (; chunk->allocated != FENCEPOST; chunk = get_right_header(chunk)) {
    if (chunk->size != get_right_header(chunk)->left_size) {
      fprintf(stderr, "Invalid sizes\n");
      print_object(chunk);
      return chunk;
    }
  }

  return NULL;
}

/**
 * @brief For each chunk allocated by the OS verify that the boundary tags
 *        are consistent
 *
 * @return true if the boundary tags are valid
 */
static inline bool verify_tags() {
  for (size_t i = 0; i < numOsChunks; i++) {
    header * invalid = verify_chunk(osChunkList[i]);
    if (invalid != NULL) {
      return invalid;
    }
  }

  return NULL;
}

/**
 * @brief Initialize mutex lock and prepare an initial chunk of memory for
 * allocation
 */
static void init() {
  pthread_mutex_init(&mutex, NULL);

#ifdef DEBUG
  // Manually set printf buffer so it won't call malloc when debugging the
  // allocator
  setvbuf(stdout, NULL, _IONBF, 0);
#endif

  header * block = allocate_chunk(ARENA_SIZE);

  header * prevFencePost = get_header_from_offset(block, -ALLOC_HEADER_SIZE);
  insert_os_chunk(prevFencePost);

  lastFencePost = get_header_from_offset(block, block->size);

  base = ((char *)block) - ALLOC_HEADER_SIZE;

  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    freelist->next = freelist;
    freelist->prev = freelist;
  }

  header * freelist = &freelistSentinels[N_LISTS - 1];
  freelist->next = block;
  freelist->prev = block;
  block->next = freelist;
  block->prev = freelist;
}

/*
 * External interface
 */
void * my_malloc(size_t size) {
  pthread_mutex_lock(&mutex);
  header * hdr = allocate_object(size);
  pthread_mutex_unlock(&mutex);
  return hdr;
}

void * my_calloc(size_t nmemb, size_t size) {
  return memset(my_malloc(size * nmemb), 0, size * nmemb);
}

void * my_realloc(void * ptr, size_t size) {
  void * mem = my_malloc(size);
  memcpy(mem, ptr, size);
  my_free(ptr);
  return mem;
}

void my_free(void * p) {
  pthread_mutex_lock(&mutex);
  deallocate_object(p);
  pthread_mutex_unlock(&mutex);
}

bool verify() { return verify_freelist() && verify_tags(); }
