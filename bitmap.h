// Maximum depth of the tree. Maximum number of bits the tree can handle is 64 ^ (MAX_DEPTH + 2).
// A MAX_DEPTH of 3 can therefore index 1 073 741 824 bits.
#define MAX_DEPTH  3

struct sparse_bitmap {
    long *data; // makes sense..
};

struct sparse_bitmap* init_bitmap(void);
void delete_bitmap(struct sparse_bitmap *bm);

int get_bit(struct sparse_bitmap *bm, unsigned int bit);
void set_bit_value(struct sparse_bitmap *bm, unsigned int bit, int value);

#define set_bit(bm, bit) set_bit_value(bm, bit, 1)
#define unset_bit(bm, bit) set_bit_value(bm, bit, 0)
