#ifndef _LINUX_RANGE_LOCK_H
#define _LINUX_RANGE_LOCK_H

#include <linux/spinlock.h>

typedef long int range_flag_t;
typedef struct rlnode_s *rlnode;
typedef struct range_lock_s *range_lock;


struct rlnode_s {
    range_flag_t key;
    rlnode forward[1];
};

struct range_lock_s {
    spinlock_t mutex;
    int level;
    struct rlnode_s *header;

    /* Random number for skip list */
    int rand_left;
    int rand_bits;
    long cnt;
};

void range_lock_init(range_lock l);
void range_lock_destroy(range_lock l);
extern void unlock_range(range_lock l, unsigned long start);
extern int try_lock_range(range_lock l, unsigned long start, size_t len);
void lock_range(range_lock l, unsigned long start, size_t len);
#endif
