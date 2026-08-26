// Physical memory allocator with per-CPU free lists, COW references,
// and on-demand 2 MiB contiguous-page allocation.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

struct {
  struct spinlock lock;
  int ref[(PHYSTOP - KERNBASE) / PGSIZE];
} krefs;

struct spinlock superops;

static int
refindex(uint64 pa)
{
  return (pa - KERNBASE) / PGSIZE;
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p = (char*)PGROUNDUP((uint64)pa_start);
  int id = 0;

  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    memset(p, 1, PGSIZE);
    struct run *r = (struct run*)p;
    r->next = kmem[id].freelist;
    kmem[id].freelist = r;
    id = (id + 1) % NCPU;
  }
}

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
  initlock(&krefs.lock, "krefs");
  initlock(&superops, "superops");
  freerange(end, (void*)PHYSTOP);
}

static int
remove_free_page(uint64 pa)
{
  for(int id = 0; id < NCPU; id++){
    struct run **link = &kmem[id].freelist;
    while(*link){
      if((uint64)*link == pa){
        *link = (*link)->next;
        return 0;
      }
      link = &(*link)->next;
    }
  }
  return -1;
}

static int
is_free_page(uint64 pa)
{
  for(int id = 0; id < NCPU; id++)
    for(struct run *r = kmem[id].freelist; r; r = r->next)
      if((uint64)r == pa)
        return 1;
  return 0;
}

void *
superalloc(void)
{
  uint64 first = SUPERPGROUNDUP((uint64)end);
  void *result = 0;

  acquire(&superops);
  push_off();
  for(int id = 0; id < NCPU; id++)
    acquire(&kmem[id].lock);
  acquire(&krefs.lock);

  for(uint64 base = first; base + SUPERPGSIZE <= PHYSTOP;
      base += SUPERPGSIZE){
    int free = 1;
    for(uint64 pa = base; pa < base + SUPERPGSIZE; pa += PGSIZE)
      if(krefs.ref[refindex(pa)] != 0){
        free = 0;
        break;
      }
    if(!free)
      continue;

    for(uint64 pa = base; pa < base + SUPERPGSIZE; pa += PGSIZE)
      if(!is_free_page(pa)){
        free = 0;
        break;
      }
    if(!free)
      continue;

    for(uint64 pa = base; pa < base + SUPERPGSIZE; pa += PGSIZE){
      if(remove_free_page(pa) < 0)
        panic("superalloc freelist");
      krefs.ref[refindex(pa)] = 1;
    }
    result = (void*)base;
    break;
  }

  release(&krefs.lock);
  for(int id = NCPU - 1; id >= 0; id--)
    release(&kmem[id].lock);
  pop_off();
  release(&superops);

  if(result)
    memset(result, 5, SUPERPGSIZE);
  return result;
}

void
superfree(void *pa)
{
  uint64 base = (uint64)pa;
  if(base % SUPERPGSIZE || base < SUPERPGROUNDUP((uint64)end) ||
     base + SUPERPGSIZE > PHYSTOP)
    panic("superfree");

  acquire(&superops);
  for(uint64 p = base; p < base + SUPERPGSIZE; p += PGSIZE){
    int ri = refindex(p);
    int old = __sync_fetch_and_sub(&krefs.ref[ri], 1);
    if(old < 1)
      panic("superfree ref");
  }
  int remaining = __atomic_load_n(&krefs.ref[refindex(base)], __ATOMIC_ACQUIRE);
  if(remaining > 0){
    release(&superops);
    return;
  }

  memset(pa, 1, SUPERPGSIZE);
  push_off();
  for(int n = 0; n < NCPU; n++)
    acquire(&kmem[n].lock);

  int id = 0;
  for(uint64 p = base; p < base + SUPERPGSIZE; p += PGSIZE){
    struct run *r = (struct run*)p;
    r->next = kmem[id].freelist;
    kmem[id].freelist = r;
    id = (id + 1) % NCPU;
  }

  for(int n = NCPU - 1; n >= 0; n--)
    release(&kmem[n].lock);
  pop_off();
  release(&superops);
}

void
superrefinc(uint64 pa)
{
  uint64 base = SUPERPGROUNDDOWN(pa);
  for(uint64 p = base; p < base + SUPERPGSIZE; p += PGSIZE){
    int old = __sync_fetch_and_add(&krefs.ref[refindex(p)], 1);
    if(old < 1)
      panic("superrefinc");
  }
}

int
superrefcount(uint64 pa)
{
  return __atomic_load_n(&krefs.ref[refindex(SUPERPGROUNDDOWN(pa))],
                         __ATOMIC_ACQUIRE);
}

void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end ||
     (uint64)pa >= PHYSTOP)
    panic("kfree");

  int i = refindex((uint64)pa);
  int old = __sync_fetch_and_sub(&krefs.ref[i], 1);
  if(old < 1)
    panic("kfree ref");
  if(old > 1)
    return;

  memset(pa, 1, PGSIZE);
  struct run *r = (struct run*)pa;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);

  if(r == 0){
    for(int n = 1; n < NCPU && r == 0; n++){
      int donor = (id + n) % NCPU;
      acquire(&kmem[donor].lock);
      r = kmem[donor].freelist;
      if(r)
        kmem[donor].freelist = r->next;
      release(&kmem[donor].lock);
    }
  }
  pop_off();

  if(r){
    if(__sync_lock_test_and_set(&krefs.ref[refindex((uint64)r)], 1) != 0)
      panic("kalloc ref");
    memset(r, 5, PGSIZE);
  }
  return r;
}

void
krefinc(uint64 pa)
{
  if(pa < KERNBASE || pa >= PHYSTOP)
    panic("krefinc");
  if(__sync_fetch_and_add(&krefs.ref[refindex(pa)], 1) < 1)
    panic("krefinc ref");
}

int
krefcount(uint64 pa)
{
  return __atomic_load_n(&krefs.ref[refindex(pa)], __ATOMIC_ACQUIRE);
}
