// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct KMem {
  struct spinlock lock;
  struct run *freelist;
  int len;
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
    kmem[i].len = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
kfree2(void* pa, int cpuid) {
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  struct KMem* cur_kmem = &kmem[cpuid];
  acquire(&cur_kmem->lock);
  r->next = cur_kmem->freelist;
  cur_kmem->freelist = r;
  cur_kmem->len++;
  release(&cur_kmem->lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(int cnt = 0; p + PGSIZE <= (char*)pa_end; p += PGSIZE, cnt++) {
    kfree2(p, cnt % NCPU);
  }
}

// Find the CPU with the least amount of free memory.
// Would not acquire any lock, thus tiny inaccuracy might exist.
// But that's acceptable.
int
poorest_cpuid() {
  int min_cpu = 0;
  int min_len = kmem[0].len;

  for(int i = 1; i < NCPU; i++) {
    if(kmem[i].len < min_len) {
      min_len = kmem[i].len;
      min_cpu = i;
    }
  }
  return min_cpu;
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa) {
  // kfree2(pa, poorest_cpuid());
  push_off();
  kfree2(pa, cpuid());
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  struct run *r;
  struct KMem* cur_kmem = kmem + cpuid();
  
  acquire(&cur_kmem->lock);
  r = cur_kmem->freelist;
  if(r) {
    cur_kmem->freelist = r->next;
    cur_kmem->len--;
  }
  release(&cur_kmem->lock);
  
  pop_off();

  if (!r) {
    // steal
    // in fact the following method will still not be perfect,
    // for example memory lists we already checked might be allocated with new pages
    // soon, and we can miss them. But that's really rare.
    for (int i = 0; i < NCPU; i++) {
      cur_kmem = kmem + i;
      acquire(&cur_kmem->lock);
      r = cur_kmem->freelist;
      if(r) {
        cur_kmem->freelist = r->next;
        cur_kmem->len--;
        release(&cur_kmem->lock);
        break;
      }
      release(&cur_kmem->lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
