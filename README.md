# Memory Allocator

A dynamic memory allocator implementation providing `malloc`/`free` equivalents (`allocate_object`/`deallocate_object`) with segregated free lists, boundary tags, and coalescing.

## Overview

This project implements a memory manager that obtains large chunks from the OS via `sbrk` and subdivides them to satisfy allocation requests. Key design choices:

- **Segregated free lists** (59 size classes) for fast allocation
- **Boundary tags** (`left_size` in header) enabling O(1) coalescing
- **Fenceposts** to prevent coalescing across `sbrk` chunk boundaries
- **Immediate coalescing** on `free` to reduce external fragmentation
- **Splitting** of oversized free blocks to minimize internal fragmentation

The allocator targets correctness first, then space and time efficiency.

## Features

- `allocate_object(raw_size)` – allocates at least `raw_size` bytes, returns aligned pointer
- `deallocate_object(ptr)` – returns previously allocated block to the pool
- Automatic heap growth via `sbrk` when needed
- Coalescing of adjacent free blocks on deallocation
- Segregated free lists for O(√n) allocation (typically O(1) due to size classes)
- Minimal metadata overhead (header per block)
- No dependencies beyond standard C library

## Building

Requires a C compiler (GCC or Clang) and `make`.

```bash
make          # builds all test binaries
```

## Running Tests

```bash
make test     # run the full test suite
```

To run a specific test:

```bash
python3 ./runtest.py -t test_name
```

Run only the initial milestone tests:

```bash
python3 ./runtest.py initial
```

## Formatting and Linting

```bash
make format          # apply clang-format
make format-check    # verify formatting
make lint            # run clang-tidy
```

## Usage as a Library

The allocator is contained in `my_malloc.c` with the public interface in `my_malloc.h`. To use it in your own code:

```c
#define ALLOCATOR_IMPLEMENTATION   // in exactly one source file
#include "my_malloc.h"

void *p = allocate_object(256);
// use p
deallocate_object(p);
```

Ensure `ALLOCATOR_IMPLEMENTATION` is defined before including the header in one compilation unit.

## API

### `void *allocate_object(size_t raw_size)`

Allocates a block of memory suitable for holding an object of `raw_size` bytes.

- Returns a pointer suitably aligned for any fundamental type.
- Returns `NULL` if the request cannot be satisfied (should not happen under normal operation; the allocator will request more memory from the OS).
- The returned pointer is guaranteed to be at least 8‑byte aligned.

### `void deallocate_object(void *ptr)`

Returns the block pointed to by `ptr` to the pool of free memory.

- `ptr` must be a pointer previously returned by `allocate_object` and not yet deallocated.
- Passing `NULL` has no effect (standard `free` behavior).
- Passing an invalid pointer results in undefined behavior.

## Design Details

For a deeper explanation of the allocator’s internals, see the extensive comments in `my_malloc.c` and `my_malloc.h`. Highlights:

- **Header layout**: each block begins with a header containing `size`, `left_size`, and an `allocated` flag.
- **Free lists**: doubly‑linked circular lists, one per size class (8‑byte granularity up to 456 bytes, then a catch‑all list).
- **Coalescing**: on every free, the allocator checks the immediate neighbors and merges contiguous free blocks.
- **Heap growth**: when no free block fits a request, the allocator requests a new chunk via `sbrk`. If the new chunk is adjacent to the previous chunk, the intervening fenceposts are absorbed to yield extra usable space.

## Testing

The test suite exercises correctness, edge cases, and basic performance properties. Tests are located in the `tests/` directory and are driven by `runtest.py`.

## License

This project is provided as part of an educational exercise. See the repository for any specific licensing information.