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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // one kmem per cpu

void
kinit()
{
  for (int i = 0; i < NCPU; i++)
  {
    initlock(&kmem[i].lock, "kmem"); // initialize locks for each kmem
  }
  
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off(); // disable interrupts
  int cid = cpuid();  // get current cpuid
  pop_off();  // enable interrupts
  acquire(&kmem[cid].lock); // acquire current cpu kmem.lock
  r->next = kmem[cid].freelist; // record old start of the freelist in r->next
  kmem[cid].freelist = r; // set freelist equal to r
  release(&kmem[cid].lock); // release current cpu kmem.lock
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off(); // disable interrupts
  int cid = cpuid(); // get current cpuid
  pop_off(); // enable interrupts

  // allocate pages from current cpu freelist and cpus with id > current cpuid
  for (int i = cid; i < NCPU; i++)
  {
    acquire(&kmem[i].lock);
    r = kmem[i].freelist; // get current kmem freelist
    if (r) {
      kmem[i].freelist = r->next; // record new start of freelist
    }
    release(&kmem[i].lock);

    if (r) {
      memset((char*)r, 5, PGSIZE); // fill page with junk
      return (void*)r;
    }
  }

  // allocate pages from cpus with id < current cpuid
  for (int i = 0; i < cid; i++)
  {
    acquire(&kmem[i].lock);
    r = kmem[i].freelist; // get current kmem freelist

    if (r) {  // if freelist is not empty
      kmem[i].freelist = r->next; // record new start of freelist
    }
    release(&kmem[i].lock);

    if (r)
    {
      memset((char*)r, 5, PGSIZE);  // fill page with junk
      return (void*)r;
    }
  }
  return (void*)r;
}
