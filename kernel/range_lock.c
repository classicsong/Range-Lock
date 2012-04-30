#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/range_lock.h>
#include <linux/random.h>
#include <linux/sched.h>

#define BITS_IN_RANDOM 31
#define NUM_LEVELS 16
#define MAX_LEVEL (NUM_LEVELS-1)

#define succ(n) (n->forward[0])
#define new_node(l) (rlnode)kmalloc(sizeof(struct rlnode_s)+(l)*sizeof(rlnode *), GFP_KERNEL)


/* Speicial NIL node */
struct rlnode_s nil = { 0x7fffffffffffffff, {&nil} };
rlnode NIL = &nil;

void range_lock_init(range_lock l)
{
    int i;
    rlnode start, end;

    l->level = 0;
    l->header = new_node(NUM_LEVELS);
    spin_lock_init(&l->mutex);
    l->rand_bits = 0;
    l->rand_left = 0;
    l->cnt = 0;

    for (i = 0; i < NUM_LEVELS; i++)
        l->header->forward[i] = NIL;

    /* Insert init node */
    start = new_node(0);
    end = new_node(0);
    succ(start) = end;
    succ(end) = succ(l->header);
    succ(l->header) = start;
};

void range_lock_destroy(range_lock l)
{
    rlnode p, q;

    if (l->cnt != 0)
        printk("error unlock %ld\n", l->cnt);
    p = l->header;
    do {
        q = p->forward[0];
        kfree(p);
        p = q;
    }
    while (p != NIL);
    kfree(l);
};

inline static int random_level(range_lock l)
{
    int level = 0;
    int b;
    do {
        if (l->rand_left == 0) {
            l->rand_bits = get_random_int();
            l->rand_left = BITS_IN_RANDOM / 2;
        };
        b = l->rand_bits & 3;
        if (!b)
            level++;
        l->rand_bits >>= 2;
        l->rand_left--;
    } while (!b);
    return (level > MAX_LEVEL ? MAX_LEVEL : level);
};

inline static void find_preds(range_lock l, range_flag_t key, rlnode * preds)
{
    int k;
    rlnode p, q;
    p = l->header;
    k = l->level;
    do {
        while (q = p->forward[k], q->key < key)
            p = q;
        preds[k] = p;
    } while (--k >= 0);
}

inline static void insert_range_flag(range_lock l, rlnode * preds,
                              range_flag_t start_flag, range_flag_t end_flag)
{
    int k;
    rlnode p, q;

    /* Adjust skip list level */
    k = random_level(l);
    if (k > l->level) {
        k = ++l->level;
        preds[k] = l->header;
    };

    /* Insert start_flag at random level */
    q = new_node(k);
    q->key = start_flag;
    do {
        p = preds[k];
        q->forward[k] = p->forward[k];
        p->forward[k] = q;
    } while (--k >= 0);

    /* Insert end_flag at level 0 */
    p = q;
    q = new_node(0);
    q->key = end_flag;
    succ(q) = succ(p);
    succ(p) = q;
}

inline static void delete_range_flag(range_lock l, rlnode * preds)
{
    int k, m;
    rlnode p = NULL;
    rlnode q, q2;
    m = l->level;
    q = succ(preds[0]);

    /* Delete start_flag */
    for (k = 0; k <= m && (p = preds[k])->forward[k] == q; k++)
        p->forward[k] = q->forward[k];

    /* Delete end_flag (in level 0) */
    q2 = succ(q);
    succ(p) = succ(q2);
    kfree(q);
    kfree(q2);

    /* Adjust skip list level */
    while (l->header->forward[m] == NIL && m > 0)
        m--;
    l->level = m;
}

void unlock_range(range_lock l, unsigned long start)
{
    unsigned long flag;
    range_flag_t start_flag;
    rlnode preds[NUM_LEVELS];

    spin_lock_irqsave(&l->mutex, flag);

    l->cnt --;
    /* Locate predecessor in each level */
    start_flag = (start << 1) + 1;
    find_preds(l, start_flag, preds);

    /* Delete from list */
    delete_range_flag(l, preds);
    spin_unlock_irqrestore(&l->mutex, flag);
}

int try_lock_range(range_lock l, unsigned long start, size_t len)
{
    unsigned long flag;
    range_flag_t end_flag = ((start + len) << 1);
    range_flag_t start_flag = (start << 1) + 1;
    rlnode preds[NUM_LEVELS];
    rlnode n, succ_node;

    spin_lock_irqsave(&l->mutex, flag);

    /* Locate predecessor in each level */
    find_preds(l, start_flag, preds);

    /* Test if their is a free range */
    n = preds[0];
    if ((n->key & 1) == 1) {
        spin_unlock_irqrestore(&l->mutex, flag);
        return 0;
    }
    succ_node = succ(n);

    if (succ_node->key <= end_flag) {
        spin_unlock_irqrestore(&l->mutex, flag);
        return 0;
    }

    /* Insert the range to list */
    insert_range_flag(l, preds, start_flag, end_flag);
    l->cnt ++;
    spin_unlock_irqrestore(&l->mutex, flag);
    return 1;
}

void lock_range(range_lock l, unsigned long start, size_t len)
{
    while(!try_lock_range(l, start, len)) {
        yield();
    }
}
