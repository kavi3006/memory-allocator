# Lab: Implementing a Memory Allocator

In this lab you will implement the two core functions of a dynamic memory
allocator — `allocate_object` (the heart of `malloc`) and `deallocate_object`
(the heart of `free`) — along with a few small helpers they depend on.

---

## Table of Contents

1. [Background](#background)
   - [What is a memory allocator?](#what-is-a-memory-allocator)
   - [The Heap and the Program Break](#the-heap-and-the-program-break)
   - [Memory and Paging](#memory-and-paging)
   - [Why Not Call sbrk Directly?](#why-not-call-sbrk-directly)
2. [Key Concepts](#key-concepts)
   - [Fragmentation](#fragmentation)
   - [Terminology](#terminology)
   - [Allocation Strategies](#allocation-strategies)
   - [Utilization vs Throughput](#utilization-vs-throughput)
3. [Design](#design)
   - [The Block Header](#the-block-header)
   - [Memory Layout: Chunks and Fenceposts](#memory-layout-chunks-and-fenceposts)
   - [Segregated Free Lists](#segregated-free-lists)
   - [Allocation](#allocation)
   - [Deallocation and Coalescing](#deallocation-and-coalescing)
   - [Requesting More Memory from the OS](#requesting-more-memory-from-the-os)
4. [Your Tasks](#your-tasks)
5. [Getting Started](#getting-started)
   - [Prerequisites](#prerequisites)
   - [Build](#build)
   - [Run all tests](#run-all-tests)
   - [Format your code](#format-your-code)
   - [Lint your code](#lint-your-code)
   - [Docker](#docker)
6. [Rubric](#rubric)

---

## Background

### What is a memory allocator?

When a C program calls `malloc(n)`, it receives a pointer to `n` bytes of
usable memory. When it calls `free(p)`, those bytes are returned for future
use. The standard C library provides these functions, but underneath they are
just a user-space library that manages a large region of memory obtained from
the operating system.

```
  User program          Memory allocator        Operating system
  ──────────────        ─────────────────       ────────────────
  malloc(24)   ──────▶  find a 24-byte slot  ──▶  sbrk() (if needed)
               ◀──────  return pointer        ◀──  returns 4096 bytes
  free(p)      ──────▶  mark slot available
               ◀──────  (no return value)
```

A good allocator must be:
- **Correct** — every returned pointer is valid and non-overlapping.
- **Space-efficient** — internal and external fragmentation are minimised.
- **Fast** — allocation and deallocation should run in roughly constant time.

### The Heap and the Program Break

Every process has a region of memory called the **heap** that grows upward as
the program requests more dynamic memory. The end of this region is called the
**program break**.

Two syscalls control it:

```
  brk(addr)   Set the program break to addr. Returns 0 on success, -1 on error.

  sbrk(n)     Increment the program break by n bytes.
              Returns the OLD break value (i.e. a pointer to the newly
              usable memory). Passing n=0 queries the current break without
              moving it.
```

```
  Virtual address space of one process

  ┌───────────────────┐  ← high addresses
  │       Stack       │  (grows downward)
  │         ↓         │
  │   (unmapped gap)  │
  │         ↑         │
  │       Heap        │  (grows upward)
  │ ──────────────── ←┼── program break  (moved by sbrk)
  │   Data / BSS      │
  │   Text (code)     │
  └───────────────────┘  ← low addresses (0x0)
```

When you call `sbrk(4096)`, the program break moves up by 4096 bytes and you
get back a pointer to the start of that new region. That memory is yours to
use however you like — the OS just gave you the address range.

### Memory and Paging

The OS does not hand out individual bytes or even individual words. It manages
physical RAM in fixed-size units called **pages** (almost universally 4 KB on
modern hardware). When you call `sbrk`, the kernel extends your virtual address
space mapping — but it does **not** immediately wire up physical RAM.

This is called **demand paging**: physical frames are allocated lazily, only
when the process first accesses each page. That access triggers a **page
fault**, the kernel maps a physical frame to the virtual page, and execution
resumes transparently.

```
  Virtual pages (what your process sees)
  ┌──────────┬──────────┬──────────┬──────────┐
  │  page 0  │  page 1  │  page 2  │  page 3  │   4 KB each
  └────┬─────┴────┬─────┴──────────┴──────────┘
       │          │            ↑ not yet accessed → no physical frame
       ▼          ▼
  ┌──────────┬──────────┐
  │ frame 7  │ frame 12 │   Physical RAM (frames are same size as pages)
  └──────────┴──────────┘
       ↑ page table maps virtual pages to physical frames
```

Key consequences for the allocator:
- **sbrk is the cheapest way to get heap memory** — one syscall extends the
  virtual mapping; the kernel does the rest on demand.
- **The minimum OS-level unit is a page (4 KB)**. Even `sbrk(1)` wastes
  almost an entire page of physical RAM once you touch it.
- **Memory can only be returned to the OS in whole pages**. You cannot give
  back 24 bytes — only entire 4 KB-aligned ranges.

### Why Not Call sbrk Directly?

Given that `sbrk` exists, why not have every `malloc` call just call `sbrk`
and every `free` call just call `sbrk` in reverse?

**1. System calls are expensive.**  
Each `sbrk` invocation crosses the user/kernel boundary — saving registers,
switching privilege levels, executing kernel code, and returning. This takes
hundreds of nanoseconds. A busy program calling `malloc` millions of times per
second cannot afford one syscall per allocation.

**2. The page granularity mismatch.**  
`sbrk(24)` doesn't give you 24 bytes — the OS ultimately backs it with at
least a 4 KB page. Calling `sbrk` for every small allocation wastes RAM and
system call overhead at the same time.

**3. sbrk can only grow or shrink at the top.**  
The heap is a contiguous range. You can move the break up (allocate) or down
(free from the very top), but you cannot punch a hole in the middle. Once you
have allocations at different addresses, `sbrk` cannot free any of them
individually.

**4. No structure.**  
`sbrk` gives you a raw block of bytes with no bookkeeping. You immediately
need to track which parts are in use, how large each region is, and how to
find free space quickly — which is exactly what a memory allocator does.

The allocator's solution: call `sbrk` **once per large chunk** (e.g. 4 KB at
a time), then sub-divide and track that chunk in user space using the free-list
structures described in the Design section.

> **Note on `mmap`**: Modern allocators like glibc's `malloc` use `mmap`
> instead of `sbrk` for large allocations (typically > 128 KB). `mmap` lets
> the allocator place memory anywhere in the address space and, crucially,
> return it to the OS with `munmap` when the allocation is freed — something
> `sbrk` cannot do for blocks in the middle of the heap. This allocator uses
> `sbrk` only, for simplicity.

---

## Key Concepts

### Fragmentation

Fragmentation is the primary enemy of a memory allocator. There are two kinds.

**External fragmentation** — free memory exists but is split into small
non-contiguous pieces, making it impossible to satisfy a large request even
though total free memory is sufficient.

```
  Heap state:
  [USED 10B][FREE 5B][USED 20B][FREE 5B][USED 10B]

  malloc(8) → FAILS
  // 10 bytes are free in total, but no single contiguous free block is ≥ 8B
```

External fragmentation is caused by interleaving allocations and frees of
different sizes. It is fought with **coalescing** (merging adjacent free
blocks) and **splitting** (avoiding oversized allocations).

**Internal fragmentation** — the allocator gives you more memory than you
asked for (due to alignment or minimum block-size constraints), wasting the
leftover bytes inside the block.

```
  malloc(13) → allocator returns a 16B block  (rounds up to 8-byte boundary)
  // 3 bytes are wasted inside the block, inaccessible to both user and allocator
```

Internal fragmentation is inherent to any aligned allocator. It is minimised
by choosing alignment granularity carefully (8 bytes in this allocator) and by
not rounding up more than necessary.

### Terminology

| Term | Definition |
|------|------------|
| **Heap** | The contiguous region of virtual memory that the allocator manages, grown by moving the program break via `sbrk()` (or mapped via `mmap()`). |
| **Free list** | The data structure tracking blocks available for allocation. Common forms: unsorted linked list, segregated size-class lists (this allocator), bitmap, or red-black tree. |
| **Coalescing** | Merging two or more adjacent free blocks into one larger block to reduce external fragmentation. `[FREE 8B][FREE 16B]` → `[FREE 24B]`. |
| **Splitting** | When a free block is larger than needed, divide it: give the user the right-sized piece and return the remainder to the free list. Reduces internal fragmentation. |
| **Alignment** | Blocks must begin at addresses divisible by some value (8 or 16 bytes is typical) so that the CPU can load values from them efficiently. Misaligned accesses are illegal on some architectures and slow on others. Alignment is the main cause of internal fragmentation. |
| **Header** | Metadata stored at the beginning of each heap block: at minimum, the block's size and allocation status. Lets the allocator navigate blocks without a separate index. |
| **Footer** | A copy of the header stored at the *end* of each block. With both header and footer, the allocator can walk the heap backwards in O(1) — used by some designs for coalescing. This allocator uses `left_size` in the header instead, achieving the same result with less overhead. |
| **Boundary tags** | The combination of header + footer (or equivalent) that allows O(1) navigation in both directions through the heap. Coined by Knuth. |
| **Fencepost** | A permanently-allocated sentinel block placed at the boundaries of each OS chunk. Stops coalescing from crossing chunk boundaries. |
| **Bin / size class** | A bucket in a segregated free list, grouping blocks of a specific size range. Searching starts at the bin matching the request size, making average-case lookup fast. |
| **Program break** | The address of the first byte past the end of the heap. Moved upward by `sbrk(n)` to expand the heap. |

### Allocation Strategies

When `malloc` searches the free list for a block to satisfy a request, there
are several strategies for which block to pick:

| Strategy | How it works | Trade-off |
|----------|-------------|-----------|
| **First fit** | Take the first free block that is large enough, starting from the front of the list. | Fast. Tends to fragment the low end of the heap, leaving many small blocks. |
| **Best fit** | Scan the entire free list, take the smallest block that still fits. | Minimises wasted space per allocation, but requires a full scan and tends to leave tiny unusable fragments. |
| **Worst fit** | Take the largest available block. | The leftover after splitting is maximally large, which sounds good in theory, but in practice leads to poor utilisation. Rarely used. |
| **Next fit** | Like first fit, but remember where the last search ended and start there next time. | Spreads fragmentation more evenly across the heap instead of concentrating it at the front. |

This allocator uses a form of **first fit within each size class** (segregated
fit): it jumps directly to the size-class bin for the requested size, then
takes the first block in that bin. This gives near-best-fit quality with
near-first-fit speed.

### Utilization vs Throughput

Every allocator design makes a trade-off between two competing goals:

**Throughput** — the number of `malloc`/`free` operations completed per
second. High throughput means low overhead per call. A simple bump allocator
that never frees has maximum throughput.

**Utilization** — how efficiently the heap's bytes are used. Defined as the
ratio of bytes in live use to total bytes requested from the OS. A utilization
of 100% would mean zero fragmentation and zero allocator overhead — impossible
in practice.

```
  High throughput ←──────────────────────────────────► Low throughput
  Low utilization                                       High utilization

  [bump allocator]  [first fit]  [segregated fit]  [best fit + compaction]
```

Optimising one almost always hurts the other:
- Keeping a sorted free list for best-fit quality costs time on every `free`.
- Splitting aggressively keeps utilisation high but creates more list entries
  to search through, slowing future allocations.
- Coalescing immediately on `free` (what this allocator does) maximises
  utilisation but adds work to each `free` call.

A real-world allocator (like jemalloc or tcmalloc) tunes this trade-off
carefully using benchmarks across many workloads. For this lab, correctness
comes first; efficiency is a secondary goal.

---

## Design

This allocator uses **boundary tags**, **fenceposts**, and **segregated free
lists**. Each concept is described below.

### The Block Header

Every region of memory managed by the allocator — whether free or allocated —
begins with a **header** that stores metadata about that block.

```
  ┌──────────────────────────────────────────────────────────────┐
  │                         header                               │
  ├───────────────────┬──────────────────┬───────────────────────┤
  │  size  (8 bytes)  │ left_size (8 B)  │  allocated  (4 B)     │
  │  total block size │ size of the      │  UNALLOCATED = 0      │
  │  including header │ block to the     │  ALLOCATED   = 1      │
  │                   │ left in memory   │  FENCEPOST   = 2      │
  ├───────────────────┴──────────────────┴───────────────────────┤
  │  (when UNALLOCATED / free)                                   │
  │    next  ──▶  next block in the free list  (8 bytes)         │
  │    prev  ──▶  prev block in the free list  (8 bytes)         │
  ├──────────────────────────────────────────────────────────────┤
  │  (when ALLOCATED)                                            │
  │    data[0]  ◀── user pointer returned by malloc starts here  │
  └──────────────────────────────────────────────────────────────┘
```

Key constant: `ALLOC_HEADER_SIZE` is the number of bytes from the *start* of
the header to the *start* of `data[]` (i.e. the size of the always-present
fields: `size`, `left_size`, `allocated`). The `next`/`prev` pointers occupy
the same bytes as `data[]` — this union is intentional; when a block is free
those bytes hold list pointers, and when it is allocated they hold user data.

The `left_size` field lets us navigate to the block immediately to our left
without storing a second pointer. This is the "boundary tag" part of the
design, enabling O(1) coalescing.

```
      get_left_header(h)              get_right_header(h)
             │                                 │
             ▼                                 ▼
  ┌──────────┬──────────────────────┬──────────┐
  │  LEFT    │         h            │  RIGHT   │
  │ (size=L) │  left_size = L       │          │
  └──────────┴──────────────────────┴──────────┘
             ◀── h - left_size      h + size ──▶
```

### Memory Layout: Chunks and Fenceposts

The allocator requests memory from the OS using `sbrk()` (which extends the
program's heap). Each request returns one **chunk** of `ARENA_SIZE` bytes.
Chunks are arranged contiguously in memory as the heap grows.

Each chunk is bookended by two **fenceposts** — special header-sized blocks
with `allocated = FENCEPOST`. Fenceposts act as permanent sentinels: the
coalescing logic in `free` checks `allocated` on its neighbours, and a
fencepost tells it "stop here — do not merge across this boundary."

```
  Low address                                         High address
  ↓                                                              ↓

  ┌────┬─────────────────────────────────────────────┬────┐
  │ FP │        usable blocks (free or allocated)    │ FP │
  │    │                                             │    │
  └────┴─────────────────────────────────────────────┴────┘
  ◄──────────────────── OS Chunk 0 ────────────────────────►

  ┌────┬─────────────────────────────────────────────┬────┐
  │ FP │        usable blocks (free or allocated)    │ FP │
  │    │                                             │    │
  └────┴─────────────────────────────────────────────┴────┘
  ◄──────────────────── OS Chunk 1 ────────────────────────►

  FP = Fencepost  (allocated = FENCEPOST, size = ALLOC_HEADER_SIZE)
```

When a new chunk from `sbrk()` arrives directly adjacent to the previous
chunk, the two inner fenceposts are absorbed and the blocks on either side
are merged. This is explained further in
[Requesting More Memory from the OS](#requesting-more-memory-from-the-os).

### Segregated Free Lists

Free blocks are organised into **N_LISTS = 59** doubly-linked circular lists,
one per size class. List `i` holds free blocks whose usable-data size is
exactly `(i + 1) * 8` bytes (except for the last list, which holds all
blocks too large for any earlier list).

```
  Index  Usable data size     Total block size
  ─────  ────────────────     ────────────────
    0       8 bytes           8  + ALLOC_HEADER_SIZE
    1      16 bytes           16 + ALLOC_HEADER_SIZE
    2      24 bytes           24 + ALLOC_HEADER_SIZE
   ...
   57     456 bytes           456 + ALLOC_HEADER_SIZE
   58     ≥ 464 bytes         any large block

  freelistSentinels[i]  ←──────────────────────────────────────────┐
       next ──▶ [ block A ] ←──▶ [ block B ] ←──▶ [ block C ] ──▶  ┘
       prev ◀── [ block A ] ◀──▶ [ block B ] ◀──▶ [ block C ] ◀──  ┘
```

Each list is **circular**: the sentinel node's `next` points to the first
real block and `prev` points to the last; when the list is empty both point
back to the sentinel itself.

The size-class index for a block of total size `s` is:
```
  index = ((s - ALLOC_HEADER_SIZE) / 8) - 1   clamped to [0, N_LISTS - 1]
```

Segregated lists mean allocation is fast: for a request of size `s`, start
searching at list `index` and walk upward — the first non-empty list
guaranteed to have a fitting block is found quickly.

### Allocation

`allocate_object(raw_size)` implements `malloc`. The algorithm is:

**Step 1 — Size calculation**

Round the request up to an 8-byte-aligned block size that also fits the
header:

```
  requestSize = ceil((raw_size + ALLOC_HEADER_SIZE) / 8) * 8
  requestSize = max(requestSize, sizeof(header))
```

**Step 2 — Search free lists**

Start at `index = (requestSize / 8) - 1` (clamped to `N_LISTS - 1`) and
walk each list until a fitting block is found.

Three sub-cases:

```
  a) Exact fit  (hdr->size == requestSize)
     ┌──────────────────────────────┐
     │  hdr  UNALLOCATED  size=R    │   →   mark ALLOCATED, unlink, return
     └──────────────────────────────┘

  b) Too-small remainder  (hdr->size - requestSize < sizeof(header))
     The leftover would be too small to hold a header, so treat as exact fit.

  c) Splittable  (hdr->size - requestSize >= sizeof(header))

     Before:
     ┌──────────────────────────────────────────────────┐
     │              hdr   size = S   UNALLOCATED        │
     └──────────────────────────────────────────────────┘

     After (carve R bytes off the RIGHT end):
     ┌────────────────────────┬─────────────────────────┐
     │  hdr  size = S-R       │  new  size = R          │
     │  UNALLOCATED           │  ALLOCATED              │
     │  (moved to new list)   │  data[] ← returned ptr  │
     └────────────────────────┴─────────────────────────┘
```

Carving from the right keeps `hdr` in roughly the same location, which
simplifies updating the `left_size` of the block to the right of the
new allocation.

**Step 3 — Request more memory**

If no free block fits, call `allocate_chunk(ARENA_SIZE)` and handle the
adjacent/non-adjacent cases described in the next section.

### Deallocation and Coalescing

`deallocate_object(p)` implements `free`. The key concern is **fragmentation**:
if every freed block stays isolated, the allocator accumulates many small
unusable gaps. The solution is to **coalesce** (merge) adjacent free blocks
immediately on each `free` call.

After recovering the block header via `ptr_to_header(p)`, inspect the
immediate left and right neighbours. There are four cases:

```
  Case A — both neighbours free  ──────────────────────────────────

    Before:              free(B)            After:
    ┌──────┬──────┬──────┐      ──────▶    ┌────────────────────┐
    │  A   │  B   │  C   │                 │      A + B + C     │
    │ FREE │ ALLOC│ FREE │                 │    UNALLOCATED     │
    └──────┴──────┴──────┘                 └────────────────────┘
    Merge all three. Unlink C from its list. Move A to new list if
    its size class changed.

  Case B — only right neighbour free  ─────────────────────────────

    Before:              free(B)            After:
    ┌──────┬──────┬──────┐      ──────▶    ┌──────┬─────────────┐
    │  A   │  B   │  C   │                 │  A   │    B + C    │
    │ ALLOC│ ALLOC│ FREE │                 │ ALLOC│ UNALLOCATED │
    └──────┴──────┴──────┘                 └──────┴─────────────┘
    Absorb C into B. Inherit C's list links. Move B if size class changed.

  Case C — only left neighbour free  ──────────────────────────────

    Before:              free(B)            After:
    ┌──────┬──────┬──────┐      ──────▶    ┌─────────────┬──────┐
    │  A   │  B   │  C   │                 │    A + B    │  C   │
    │ FREE │ ALLOC│ ALLOC│                 │ UNALLOCATED │ ALLOC│
    └──────┴──────┴──────┘                 └─────────────┴──────┘
    Absorb B into A. Move A to new list if size class changed.

  Case D — neither neighbour free  ────────────────────────────────

    Before:              free(B)            After:
    ┌──────┬──────┬──────┐      ──────▶    ┌──────┬──────┬──────┐
    │  A   │  B   │  C   │                 │  A   │  B   │  C   │
    │ ALLOC│ ALLOC│ ALLOC│                 │ ALLOC│ FREE │ ALLOC│
    └──────┴──────┴──────┘                 └──────┴──────┴──────┘
    Simply insert B into the appropriate free list.
```

After merging, compute the final block's size-class index and insert it at
the front of `freelistSentinels[index]`.

### Requesting More Memory from the OS

When no free block can satisfy a request, `allocate_chunk(ARENA_SIZE)` calls
`sbrk()` to extend the heap. The newly returned chunk may or may not be
adjacent to the previous chunk in memory.

**Non-adjacent** (common in multi-threaded programs or after large gaps):

```
  Old heap:    ... [block] [FP_old_right]
                                           ← gap (other sbrk user) →
  New chunk:                               [FP_new_left] [new_block] [FP_new_right]

  Action: register new chunk with insert_os_chunk(FP_new_left),
          add new_block to freelistSentinels[N_LISTS - 1], retry.
```

**Adjacent** (typical case — the heap grows contiguously):

```
  Old heap:    ... [LEFT_BLOCK] [FP_old_right]
  New chunk:                   [FP_new_left]  [new_block] [FP_new_right]
                               ▲▲▲▲▲▲▲▲▲▲▲▲▲
                               these two fenceposts overlap at the boundary
```

The two inner fenceposts (`FP_old_right` and `FP_new_left`) are absorbed.
This yields `2 * ALLOC_HEADER_SIZE` extra bytes of usable space.

Sub-case 1 — `LEFT_BLOCK` is **ALLOCATED**:
```
  Create a new free block:
  [new free block spanning FP_old_right + FP_new_left + new_block]
  Insert into freelistSentinels[N_LISTS - 1].  Update lastFencePost.
```

Sub-case 2 — `LEFT_BLOCK` is **UNALLOCATED** (already free):
```
  Extend LEFT_BLOCK to absorb FP_old_right + FP_new_left + new_block.
  Move LEFT_BLOCK to the correct size-class list if its index changed.
  Update lastFencePost.
```

In both sub-cases, call `allocate_object(raw_size)` recursively after
inserting the new memory.

---

## Your Tasks

You must implement the following five functions in `my_malloc.c`.
All other functions are provided for you.

| Function                | Points | Description                                      |
|-------------------------|--------|--------------------------------------------------|
| `my_ceil`               |  5     | Integer ceiling without `math.h`                 |
| `calculate_actual_size` | 10     | Aligned block size from raw user request         |
| `ptr_to_header`         |  5     | Recover header pointer from user pointer         |
| `allocate_object`       | 40     | Core malloc (search, split, request new chunk)   |
| `deallocate_object`     | 40     | Core free (coalesce, re-insert into free list)   |

Read the `TODO` comments in each function carefully — they spell out the
exact steps required.

**You may not modify** any other function, any header file, the Makefile,
or the test files.

---

## Getting Started

### Prerequisites

- GCC (C11 or later)
- Python 3 (for the test runner)
- `clang-format` and `clang-tidy` (for formatting and linting)

### Build

```bash
make          # builds all test binaries
```

### Run all tests

```bash
make test
```

Or run a single test manually:

```bash
python3 ./runtest.py -t test_exact
```

Or run just the initial milestone tests:

```bash
python3 ./runtest.py initial
```

### Compile and run a test binary directly

```bash
# build first
make

# run directly (output goes to stdout/stderr)
./tests/test_simple0

# diff against expected output
./utils/difftest.sh test_simple0
```

### Format your code

```bash
make format          # reformat in-place
make format-check    # check without modifying (exits non-zero if unformatted)
```

The style is defined in `.clang-format` (2-space indent, K&R braces). Run
`make format` before submitting so your code is consistently formatted.

### Lint your code

```bash
make lint
```

Linting runs `clang-tidy` with the checks defined in `.clang-tidy`. Address
any warnings before submitting.

### Docker

A Docker environment is provided for reproducible builds and testing on any
host OS. It uses Alpine Linux with a glibc compatibility shim (`gcompat`) so
that the pre-compiled reference binaries in `tests/expected/` — which are
glibc-linked x86-64 ELF binaries — run correctly inside the container.

**Build the image:**

```bash
docker build -t memory-allocator .
```

> **Apple Silicon / ARM64 hosts:** the reference binaries are x86-64 only.
> Add `--platform linux/amd64` to enable emulation via Rosetta / QEMU:
>
> ```bash
> docker build --platform linux/amd64 -t memory-allocator .
> ```
>
> Use the same flag on every `docker run` command below.

**Run all tests:**

```bash
docker run --rm memory-allocator
```

**Run a single test:**

```bash
docker run --rm memory-allocator python3 ./runtest.py -t test_exact
```

**Run the initial milestone suite:**

```bash
docker run --rm memory-allocator python3 ./runtest.py initial
```

**Open an interactive shell** (useful for debugging inside the container):

```bash
docker run --rm -it memory-allocator sh
```

**Iterative development** — mount your local source directory so edits are
reflected immediately without rebuilding the image:

```bash
docker run --rm -it -v "$(pwd)":/app memory-allocator sh
# inside the container:
make tests && python3 ./runtest.py
```

---

## Rubric

| Category                            | Points | Tests                                              |
|-------------------------------------|--------|----------------------------------------------------|
| **Size helpers**                    |        |                                                    |
| `my_ceil` + `calculate_actual_size` |  5     | `test_malloc_zero`                                 |
| `ptr_to_header`                     |  5     | Used by all free tests                             |
| **Malloc — basic**                  |        |                                                    |
| Exact fit                           | 10     | `test_exact`                                       |
| Splitting                           | 10     | `test_split`                                       |
| Multiple sequential allocations     |  5     | `test_multi_malloc`                                |
| Zero / oversized request            |  5     | `test_malloc_zero`, `test_malloc_too_large`        |
| **Malloc — new chunks**             |        |                                                    |
| Non-adjacent chunk insertion        |  5     | `test_insert_chunk`                                |
| Adjacent chunk, left allocated      |  5     | `test_coalesce_chunk_insert`                       |
| Adjacent chunk, left free           |  5     | `test_coalesce_chunk_coalesce`                     |
| Large arena                         |  5     | `test_malloc_large`                                |
| **Free — coalescing**               |        |                                                    |
| Insert only (Case D)                |  5     | `test_free_insert`                                 |
| Coalesce left (Case C)              |  8     | `test_free_left`                                   |
| Coalesce right (Case B)             |  8     | `test_free_right`                                  |
| Coalesce both (Case A)              |  9     | `test_free_both`                                   |
| Alternating free patterns           | 10     | `test_free_even`, `test_free_odd`                  |
| **Robustness**                      |        |                                                    |
| `free(NULL)` is a no-op             |  2     | `test_free_null`                                   |
| Double-free detection               |  3     | `test_double_free`                                 |
| Out-of-memory (sbrk fails)          |  3     | `test_out_of_ram`                                  |
| Thread safety (do not break mutex)  |  2     | `test_locks`                                       |
| **Code quality**                    |        |                                                    |
| Passes `make format-check`          |  3     | —                                                  |
| No new `make lint` warnings         |  2     | —                                                  |
| **Total**                           | **100**|                                                    |

### Grading notes

- The autograder diffs your program's output against a reference binary
  stored in `tests/expected/`. Your output must match **exactly**.
- Partial credit is awarded per test: a test either passes or fails.
- Points for `allocate_object` depend on `my_ceil`, `calculate_actual_size`,
  and `ptr_to_header` being correct first — implement the helpers before
  tackling the main functions.
- The `test_large`, `test_random`, `test_random_sizes`, and `test_very_large`
  tests run without a fixed score in the autograder but are useful for
  stress-testing your implementation.
