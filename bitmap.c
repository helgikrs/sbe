#include <linux/module.h>

#include "bitmap.h"

#define malloc vmalloc
#define free   vfree

MODULE_VERSION("0.0");
MODULE_LICENSE("GPL v2");

/*
 * A sparse bitmap made from a bit-mapped trie.
 *
 * The first 6 bits are used as an index into a 64 element array recursively
 * until MAX_DEPTH is reached. There we have a long value holding 64 booleans.
 */


/*
 * Initialize a bitmap
 */
struct sparse_bitmap* init_bitmap(void) {
    struct sparse_bitmap *bm = (struct sparse_bitmap*)malloc(sizeof(struct sparse_bitmap));
    bm->data = malloc(sizeof(long*) * 64);
    memset(bm->data, 0, sizeof(long*) * 64);
    return bm;
}

static void _delete_bitmap(long *data, int level) {
    int i;
    if(data == NULL) {
        return;
    }

    if(level == MAX_DEPTH) {
        free(data);
        return;
    }

    for(i = 0; i < 64; ++i) {
        _delete_bitmap((long*)data[i], level + 1);
    }

    free(data);
}

/*
 * Delete the bitmap and free all allocated memory.
 */
void delete_bitmap(struct sparse_bitmap *bm) {
    _delete_bitmap(bm->data, 0);
    bm->data = NULL;
    free(bm);
}


#define SET(arr, bit_number) (*arr) |= 1l << bit_number
#define UNSET(arr, bit_number) (*arr) &= ~(1l << bit_number)

/*
 * Walk down the tree, returning a pointer to the long where that bit should go.
 * the "bit" pointer is changed to the index into the long (<64).
 *
 * If noalloc is true a null pointer will be returned if that particular area
 * has not ben allocated.
 */
static long* get_block(long *data, unsigned long *bit, unsigned int level, int noalloc) {
    unsigned long shift, rest, idx;
    long **ptr;

    ptr = (long**)data;

    shift = (MAX_DEPTH - level + 1) * 6;
    rest = *bit & ~(63l << shift);
    idx = (*bit >> shift) & 63l;

    *bit = (unsigned long)rest;

    if(level == MAX_DEPTH) {
        return &data[idx];
    }

    if(ptr[idx] == NULL) {
        if(noalloc) {
            return NULL;
        } else {
            ptr[idx] = (long*)malloc(sizeof(long*) * 64);
            memset(ptr[idx], 0, sizeof(long*) * 64);
        }
    }

    return get_block(ptr[idx], bit, level + 1, noalloc);
}

// Return the value of the bit "bit".
int get_bit(struct sparse_bitmap *bm, unsigned int bit) {
    unsigned long b = bit;
    long* blk = get_block(bm->data, &b, 0, 1);

    if(blk == NULL) {
        return 0;
    }

    return !!((*blk) & 1l << b);
}

/*
 * Change the value of bit number `bit` to boolean `value`.
 */
void set_bit_value(struct sparse_bitmap *bm, unsigned int bit, int value) {
    unsigned long b = bit;
    long *blk = get_block(bm->data, &b, 0, !value);

    if(blk == NULL) {
        return;
    }

    if(value) {
        SET(blk, b);
    } else {
        UNSET(blk, b);
    }
}
