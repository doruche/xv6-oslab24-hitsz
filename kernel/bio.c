// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct {
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct spinlock locks[NBUCKETS];
  struct buf heads[NBUCKETS];

  // used when searching for a buffer to recycle in other buckets.
  // we use this to ensure the invariable that the buckets we've already
  // passed won't change while we're looking through later buckets.
  struct spinlock biglock;
} bcache;

inline static
uint
hash(uint dev, uint blockno) {
  return blockno % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;
  struct buf *head;

  initlock(&bcache.biglock, "bcache big lock");

  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.locks[i], "bcache");
    bcache.heads[i].next = &bcache.heads[i];
    bcache.heads[i].prev = &bcache.heads[i];
  }

  // Simply put all buffers in the first bucket,
  // as they can be rearranged later automatically.
  head = &bcache.heads[0];  
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = head->next;
    b->prev = head;
    initsleeplock(&b->lock, "buffer");
    head->next->prev = b;
    head->next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint bucketno = hash(dev, blockno);
  acquire(&bcache.locks[bucketno]);

  // Is the block already cached?
  for (b = bcache.heads[bucketno].next; b != &bcache.heads[bucketno]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.locks[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  
  // Not cached.
  // Find an unused buffer to recycle, from this bucket.
  for (b = bcache.heads[bucketno].prev; b != &bcache.heads[bucketno]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  release(&bcache.locks[bucketno]);

  // If not found, check other buckets.
  // I.e, stealing.
  acquire(&bcache.biglock);
  for (int i = 0; i < NBUCKETS; i++) {
    if (i == bucketno) {
      continue;
    }
    // acquire(&bcache.locks[i]);

    for (b = bcache.heads[i].prev; b != &bcache.heads[i]; b = b->prev){
      if(b->refcnt == 0) {
        // Remove from the old bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        // Insert to the new bucket
        b->next = bcache.heads[bucketno].next;
        b->prev = &bcache.heads[bucketno];
        bcache.heads[bucketno].next->prev = b;
        bcache.heads[bucketno].next = b;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        // release(&bcache.locks[i]);
        release(&bcache.biglock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    // release(&bcache.locks[i]);
  }
  release(&bcache.biglock);
  

  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bucketno = hash(b->dev, b->blockno);
  acquire(&bcache.locks[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.heads[bucketno].next;
    b->prev = &bcache.heads[bucketno];
    bcache.heads[bucketno].next->prev = b;
    bcache.heads[bucketno].next = b;
  }
  release(&bcache.locks[bucketno]);

  // acquire(&bcache.lock);
  // b->refcnt--;
  // if (b->refcnt == 0) {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  // 
  // release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.locks[hash(b->dev, b->blockno)]);
  b->refcnt++;
  release(&bcache.locks[hash(b->dev, b->blockno)]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.locks[hash(b->dev, b->blockno)]);
  b->refcnt--;
  release(&bcache.locks[hash(b->dev, b->blockno)]);
}


