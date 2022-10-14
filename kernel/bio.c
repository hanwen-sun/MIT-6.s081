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
#define NBUCKET 13
#define ihash(blockno) (blockno % NBUCKET) 

struct {
  struct spinlock lock;
  struct spinlock hashlock;
  struct buf buf[NBUF];   // buffer cache的全部空间, 直接利用，不额外分配;
  int size;     // 已分配的数量;

  struct buf bucket[NBUCKET];
  int Size[NBUCKET];
  struct spinlock hashlocks[NBUCKET];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  bcache.size = 0;
  for(int i = 0; i < NBUCKET; i++)
    bcache.Size[i] = 0;

  initlock(&bcache.lock, "bcache");
  initlock(&bcache.hashlock, "bhash");
  // Create linked list of buffers
  /*bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }*/

  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    b->next = 0;
  }

  for(int i = 0; i < NBUCKET; i++)
    initlock(&bcache.hashlocks[i], "bucket");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  // 1. 在已有的bucket中寻找;
  int idx = ihash(blockno);
  
  acquire(&bcache.hashlocks[idx]);

  int cnt = 0;
  for(b = bcache.bucket[idx].next; b; b = b->next){
    cnt++;
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.hashlocks[idx]);  // lab8-2
      acquiresleep(&b->lock);
      return b;
    }

    //printf("b->next\n");
    // printf("%d %d %d\n", cpuid(), b->dev, b->blockno);
    if(cnt > 30)
      panic("loop error!");
  }


  // 2. 不在bucket中;
  //printf("not in bucket!\n");
  acquire(&bcache.lock);
  //printf("acquire bcachelock!\n");
  // 尚未分配完;
  if(bcache.size < NBUF) {
    //printf("allocate block!\n");
    b = &bcache.buf[bcache.size++];
    release(&bcache.lock);
    // acquire(&bcache.hashlocks[idx]);
    
    b->dev = dev;
    b->valid = 0;
    b->refcnt = 1;
    b->blockno = blockno; // 这里别忘了加;

    b->next = bcache.bucket[idx].next;
    bcache.bucket[idx].next = b;
    bcache.Size[idx]++;
    release(&bcache.hashlocks[idx]);
    acquiresleep(&b->lock);
    // printf("allocate success!\n");
    return b;
  }
  
  release(&bcache.lock);
  release(&bcache.hashlocks[idx]);

  int mintime = 0x3f3f3f3f;
  struct buf *pick = b;
  int flag = 1;

  acquire(&bcache.hashlock);   // 这里一定要加hashlock!!!非常重要, 不然会有另一个线程找到和这里同样的分配的块;
  for(int i = 0; i < NBUCKET; i++) {
    acquire(&bcache.hashlocks[i]);
    b = &bcache.bucket[i];
    while(b->next) {
      if(b->next->refcnt == 0 && b->next->timestamp < mintime) {
          mintime = b->next->timestamp;
          pick = b;
          flag = 1;
      }
      b = b->next; 
    }
    release(&bcache.hashlocks[i]);
  }
  if(!flag)
    panic("no buffer!");

  int idx_ = ihash(pick->next->blockno);
  
  acquire(&bcache.hashlocks[idx_]);
  struct buf *tmp = pick->next;
  if(idx_ == idx) {
    tmp->dev = dev;
    tmp->refcnt = 1;
    tmp->blockno = blockno;
    tmp->valid = 0;
    release(&bcache.hashlocks[idx_]);
    release(&bcache.hashlock);
    acquiresleep(&tmp->lock);
    return tmp;
  }
  
  
  pick->next = pick->next->next;  // 直接删除;  这里还需要保留pick->next;
  release(&bcache.hashlocks[idx_]);
  acquire(&bcache.hashlocks[idx]);
  tmp->next = bcache.bucket[idx].next;
  bcache.bucket[idx].next = tmp;
  tmp->dev = dev;
  tmp->refcnt = 1;
  tmp->blockno = blockno;
  tmp->valid = 0;
  release(&bcache.hashlocks[idx]);
  release(&bcache.hashlock);
  acquiresleep(&tmp->lock);
  return tmp;

  panic("bget");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  //printf("begin bread!\n");
  struct buf *b;

  b = bget(dev, blockno);
  // printf("bget success!\n");
  // printf("valid: %d\n", b->valid);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  //printf("bread end!\n");

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
  // printf("begin brelse!\n");
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock); 

  int idx = ihash(b->blockno);
  acquire(&bcache.hashlocks[idx]);
  b->refcnt--;
  if(b->refcnt == 0)
    b->timestamp = ticks;

  release(&bcache.hashlocks[idx]);
}

void
bpin(struct buf *b) {
  int idx = ihash(b->blockno);
  acquire(&bcache.hashlocks[idx]);
  b->refcnt++;
  release(&bcache.hashlocks[idx]);
}

void
bunpin(struct buf *b) {
  int idx = ihash(b->blockno);
  acquire(&bcache.hashlocks[idx]);
  b->refcnt--;
  release(&bcache.hashlocks[idx]);
}


