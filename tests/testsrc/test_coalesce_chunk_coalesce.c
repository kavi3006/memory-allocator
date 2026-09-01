#include "testing.h"
#include "my_malloc.h"

int main() {
  initialize_test(__FILE__);

  void * ptr = mallocing(8, print_status, false);
  mallocing(ARENA_SIZE - 3 * ALLOC_HEADER_SIZE - 128, print_status, false);
  freeing(ptr, 8, print_status, false);
  mallocing(ARENA_SIZE - 3 * ALLOC_HEADER_SIZE - 128, print_status, false);

  finalize_test();
}
