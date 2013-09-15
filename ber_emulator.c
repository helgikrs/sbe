#include <asm/current.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/random.h>

MODULE_VERSION("0.0");
MODULE_LICENSE("GPL v2");

#define DEBUG

#define QUEUE 0

#define INFO(txt, ...) \
    printk(KERN_ALERT txt, ##__VA_ARGS__); \

/*
 * A kernel module that exposes a block device with sector size 4096
 * Every read / write call is intercepted and there is some probability (the
 * uber rate) that a read will fail.
 *
 * TODO: ....
 *
 */

/*
 * Parameters.
 *
 * There are all configurable at load time. Some (Those with S_IWUSR, like
 * uber_rate) are dynamically configurable in /sys.
 */

// Major and minor versions for the device
// These can be changed on load time.
static int sbe_major = 0;
module_param(sbe_major, int, S_IRUGO);

static char* target_device = NULL;
module_param(target_device, charp, S_IRUGO);

static int sbe_minor = 0;
module_param(sbe_minor, int, S_IRUGO);

// Whether the module should return corrupt data or not return any data at all
// (I/O error)
static bool return_bad = 0;
module_param(return_bad, bool, S_IRUGO | S_IWUSR);

// Uncorrectable bit error rate as an exponent to 10.
static int uber_rate = -14;
module_param(uber_rate, int, S_IRUGO | S_IWUSR);

// Number of pages the device has
static int number_of_pages = 25600;
module_param(number_of_pages, int, S_IRUGO);

// The size of each page.
static int page_size = 4096;
module_param(page_size, int, S_IRUGO);

// Bit field for blocks that have errors.
// When an block is written we do
int err_blocks = 0;

#define SBE_MINORS 1

// The kernel always works with a sector size of 512.
// So we have to do the appropriate translation.
#define KPAGE_SIZE 512

/* Function declarations */
int handle_ioctl(struct block_device*, fmode_t, unsigned int, unsigned long);


struct sbe_dev {
        int size;  // size in sectors
        u8 *data;

        // mutual exclusion lock
        spinlock_t lock;

#if QUEUE == 0
        struct block_device *target_bdev;
#endif

        // Gendisk and request queue structures.
        struct request_queue *queue;
        struct gendisk *gd;
};

struct sbe_dev *devices = NULL;


// The device number

struct block_device_operations sbe_fops = {
    // Should probably implement more functions ...
    .owner =     THIS_MODULE,
    .ioctl =     handle_ioctl,
};


int rand(void) {
    int i;
    get_random_bytes((char*)&i, sizeof(i));
    return i;
}

bool bad_block(int ber_exponent) {
    int t[][2] = {
        {4294967295, 4294967295},
        {4294967295, 4294967295},
        {4294967295, 4294967295},
        {4294967295, 4294860799},
        {4132863739, 1890490158},
        {1200046538, 224069025},
        {138456694, 1814519724},
        {14050716, 1168926188},
        {1407144, 1423656331},
        {140735, 784294835},
        {14073, 3117192051},
        {1407, 1609122279},
        {140, 3167478464},
        {14, 316748738},
        {1, 1749661801},
        {0, 604462910},
        {0, 60446291},
        {0, 6044629},
        {0, 604463},
        {0, 60446},
        {0, 6045},
        {0, 604},
        {0, 60},
        {0, 6},
        {0, 1}
    };

    unsigned int a = rand(), b = rand();

    if(ber_exponent < -24) {
        ber_exponent = - 24;
    } else if(ber_exponent > 0) {
        ber_exponent = 0;
    }

    return t[-ber_exponent][0] >= a && t[-ber_exponent][1] >= b;
}


/*

# ioctl function.
 * Handle geometry requests
 * Possibly later some wear level info etc..

*/
int handle_ioctl(struct block_device *bdev,
        fmode_t mode, unsigned int cmd, unsigned long arg) {
    // struct sbe_dev *dev = f->private_data;

    switch(cmd) {
        // TODO: Handle ioctl requests
        default:
            return -ENOTTY; // POSIX
    }

    return 0;
}

// Helper for flipping a `coin` and mark the block bad with some probability
static inline bool writing_block(unsigned long sector, int uber_rate) {
    if(bad_block(uber_rate)) {
        printk(KERN_NOTICE "Block number %ld marked as bad\n", sector);
        // Mark the block as bad
        err_blocks |= 1 << sector;
        return 1;
    } else {
        printk(KERN_NOTICE "Block number %ld marked as good\n", sector);
        // Mark the block as good
        err_blocks &= ~(1 << sector);
        return 0;
    }
}

// Return true if the given sector has been marked as bad
static inline bool sector_has_error(int sector) {
    return err_blocks & (1 << sector);
}

/*
   Function that handles the actual read / write request.

   This function should also implement all error emulation etc.
 */
#if QUEUE == 1
static int handle_request(struct sbe_dev *dev, int dir,
        unsigned long sector, unsigned long count, char *buffer) {

    unsigned long offset = sector * KPAGE_SIZE;
    unsigned long byte_count = count * KPAGE_SIZE;
    unsigned long sbe_sector = offset / page_size;

    if(offset + byte_count > dev->size) {
        printk(KERN_NOTICE "Reading outside device's size %ld %ld\n", offset, byte_count);
        return -EINVAL;
    }

    // Direction is the direction of transmit.
    // direction != 0 is a write request,
    // direction  = 0 is a read request.
    if(dir) {
        writing_block(sbe_sector, uber_rate);
        memcpy(dev->data + offset, buffer, byte_count);
    } else {
        if(sector_has_error(sbe_sector)) {
            // We have a bad block.
            printk(KERN_NOTICE "Reading block %ld marked as bad\n", sbe_sector);
            return -EIO;
        }

        printk(KERN_NOTICE "Reading block %ld marked as good\n", sbe_sector);
        memcpy(buffer, dev->data + offset, byte_count);
    }

    return 0;
}


static void sbe_request(struct request_queue *queue) {
    struct request *req;
    int r = 0;

    req = blk_fetch_request(queue);
    while(req) {
        struct sbe_dev *dev = req->rq_disk->private_data;

        printk(KERN_NOTICE "Request type %d to dir = %d pos = %ld count = %d (bdev = %d)\n", req->cmd_type,
                rq_data_dir(req), blk_rq_pos(req), blk_rq_cur_sectors(req), 0);

        // Should we handle any other request than REQ_TYPE_FS?
        if(req->cmd_type == REQ_TYPE_FS) {
            r = handle_request(dev, rq_data_dir(req), blk_rq_pos(req), blk_rq_cur_sectors(req), req->buffer);
        } else {
            r = -EIO;
        }

        if(!__blk_end_request_cur(req, r)) {
            req = blk_fetch_request(queue);
        }
    }
}
#else
static int handle_bio(struct sbe_dev *dev, struct bio *bio) {
    unsigned long sbe_sector = bio->bi_sector * KPAGE_SIZE / page_size;


    if(bio_data_dir(bio) == WRITE) {
        printk(KERN_NOTICE "Handling bio write. Marking sector %ld as %s\n", sbe_sector, writing_block(sbe_sector, uber_rate) ? "bad" : "good");
    } else {
        if(sector_has_error(sbe_sector)) {
            printk(KERN_NOTICE "Handling bio. Sector %ld marked as bad\n", sbe_sector);
            return -EIO;
        } else {
            printk(KERN_NOTICE "Handling bio. Sector %ld marked as good\n", sbe_sector);
        }
    }

    return 0;
}

static void handle_make_request(struct request_queue *queue, struct bio *bio) {
    int err = 0;
    struct sbe_dev *dev = queue->queuedata;

    printk(KERN_WARNING "sbe: forwarding request to another device %p %p\n", dev, dev->target_bdev);


    if(unlikely(IS_ERR(dev->target_bdev) || dev->target_bdev == NULL)) {
        printk(KERN_WARNING "sbe: bailing, bi_bdev was NULL\n");
        err = -EIO;
        goto err;
    }


    // If no errors come up, we change the bi_bdev of the bio to the target
    // device and forward the request.
    if((err = handle_bio(dev, bio)) == 0) {
        bio->bi_bdev = dev->target_bdev;
        generic_make_request(bio);
    } else {
        goto err;
    }

    return;
err:
    bio_endio(bio, err);
}
#endif

// Function to initialize the device.
// dev is a pointer to the device.
// n is the device's number.
static int init_device(struct sbe_dev *dev, int n) {
    struct block_device *bdev;

    INFO("initializing disk %d\n", n);

    // struct block_device *bdev =
    // __bdev->dev

    memset(dev, 0, sizeof(struct sbe_dev));

    if(target_device == NULL) {
        printk(KERN_WARNING "sbe: missing parameter target_device\n");
        goto err;
    } else {
        printk(KERN_WARNING "sbe: setting target device: %s\n", target_device);
    }

    dev->size = number_of_pages * page_size;
    dev->data = vmalloc(dev->size); // non-contiguous allocated pages.
    if(dev-> data == NULL) {
        printk(KERN_WARNING "sbe: could not vmalloc %d bytes\n", dev->size);
        return -ENOMEM;
    }
    memset(dev->data, 0, dev->size);
#if QUEUE == 0
    bdev = NULL;
    bdev = lookup_bdev(target_device);

    if(IS_ERR(bdev)) {
        printk(KERN_WARNING "TT: %p could not open device\n", bdev);
        goto err;
    } else {
        printk(KERN_WARNING "TT: %p SUCCESS\n", bdev);
    }

    dev->target_bdev = blkdev_get_by_dev(bdev->bd_dev, FMODE_READ|FMODE_WRITE, NULL);

    bdput(bdev);
#endif

    spin_lock_init(&dev->lock);

#if QUEUE == 1
    dev->queue = blk_init_queue(sbe_request, &dev->lock);
    blk_queue_logical_block_size(dev->queue, page_size);
#else
    dev->queue = blk_alloc_queue(GFP_KERNEL);
    if(dev->queue == NULL) {
        goto err;
    }
    blk_queue_make_request(dev->queue, handle_make_request);
#endif

    dev->queue->queuedata = dev;

    dev->gd = alloc_disk(SBE_MINORS);
    if(!dev->gd) {
        printk(KERN_WARNING "sbe: alloc_disk failed\n");
        goto err;
    }

    dev->gd->major = sbe_major;
    dev->gd->first_minor = n * SBE_MINORS;
    dev->gd->fops = &sbe_fops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf(dev->gd->disk_name, 32, "sbe%c", n + 'a');
    set_capacity(dev->gd, number_of_pages * (page_size / KPAGE_SIZE)); // Number of kernel sized sector
    add_disk(dev->gd);

    return 0;

err:
    if(dev) {
        if(dev->data) {
            vfree(dev->data);
            dev->data = NULL;
        }
    }
    return -EBUSY; // XXX: o.O
}


static int __init initialization_function(void) {
    int err;

    sbe_major = register_blkdev(sbe_major, "sbe");
    if(sbe_major <= 0) {
        printk(KERN_WARNING "sbe: unable to register for a major number\n");
        return -EBUSY;
    }

    // How many devices to register? Currently only registering one device.
    devices = kmalloc(sizeof(struct sbe_dev), GFP_KERNEL);
    if(devices == NULL) {
        goto err;
    }

    // TODO: If this fails?
    if((err = init_device(devices, 0)) != 0) {
        printk(KERN_WARNING "SBE: init failed\n");
        return err;
    } else {
        printk(KERN_NOTICE "SBE: init done\n");
    }

    return 0;

err:
    unregister_blkdev(sbe_major, "sbe");
    return -ENOMEM;
}

static void __exit cleanup_function(void) {
    struct sbe_dev *dev = devices;

    if(dev->gd) {
        del_gendisk(dev->gd);
        put_disk(dev->gd);
    }

    if(dev->queue) {
#if QUEUE == 1
        blk_cleanup_queue(dev->queue);
#else
        blk_put_queue(dev->queue);
#endif
    }

    if(dev->data) {
        vfree(dev->data);
    }

#if QUEUE == 0
    if(dev->target_bdev) {
        blkdev_put(dev->target_bdev, FMODE_READ|FMODE_WRITE);
    }
#endif

    unregister_blkdev(sbe_major, "sbe");
    kfree(devices);

    printk(KERN_ALERT "Unloading module\n");
}

module_init(initialization_function);
module_exit(cleanup_function);
