#include <asm/current.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

MODULE_VERSION("0.0");
MODULE_LICENSE("GPL v2");

#define DEBUG

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
 * uber_rate) are dynamically configuable in /sys.
 */

// Major and minor versions for the device
// These can be changed on load time.
static int sbe_major = 0;
module_param(sbe_major, int, S_IRUGO);

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
static int number_of_pages = 10;
module_param(number_of_pages, int, S_IRUGO);

// The size of each page.
static int page_size = 4096;
module_param(page_size, int, S_IRUGO);

#define SBE_MINORS 1

// The kernel always works with a sector size of 512.
// So we have to do the appropriate translation.
#define KPAGE_SIZE 512

struct sbe_dev {
        int size;  // size in sectors
        u8 *data;

        // mutual exclusion lock
        spinlock_t lock;

        // Gendisk and request queue structures.
        struct request_queue *queue;
        struct gendisk *gd;
};

struct sbe_dev *devices = NULL;


// The device number

struct block_device_operations sbe_fops = {
    // Should probabliy implement more functions ...
    .owner =     THIS_MODULE,
};

/*
   Function that handles the actual read / write request.

   This function should also implement all error emulation etc.
 */
static void handle_request(struct sbe_dev *dev, int dir,
        unsigned long sector, unsigned long count, char *buffer) {

    unsigned long offset = sector * KPAGE_SIZE;
    unsigned long byte_count = count * KPAGE_SIZE;

    if(offset + byte_count > dev->size) {
        printk(KERN_NOTICE "Reading outside device's size %ld %ld\n", offset, byte_count);
        return;
    }

    if(dir) {
        memcpy(dev->data + offset, buffer, byte_count);
    } else {
        // TODO: Here we should deal with any BER issues. Emulate corruption etc.
        memcpy(buffer, dev->data + offset, byte_count);
    }
}

static void sbe_request(struct request_queue *queue) {
    struct request *req;
    int r = 0;

    req = blk_fetch_request(queue);
    while(req) {
        struct sbe_dev *dev = req->rq_disk->private_data;

        printk(KERN_NOTICE "Request to dir = %d pos = %ld count = %d\n", rq_data_dir(req), blk_rq_pos(req), blk_rq_cur_sectors(req));

        handle_request(dev, rq_data_dir(req), blk_rq_pos(req), blk_rq_cur_sectors(req), req->buffer);

        if(!__blk_end_request_cur(req, r)) {
            req = blk_fetch_request(queue);
        }
    }
}

// Function to initialize the device.
// dev is a pointer to the device.
// n is the device's number.
static int init_device(struct sbe_dev *dev, int n) {
    INFO("initializing disk %d\n", n);

    memset(dev, 0, sizeof(struct sbe_dev));
    dev->size = number_of_pages * page_size;
    dev->data = vmalloc(dev->size); // non-contigous allocated pages.
    if(dev-> data == NULL) {
        printk(KERN_WARNING "sbe: could not vmalloc %d bytes\n", dev->size);
        return -ENOMEM;
    }

    memset(dev->data, 10, dev->size * sizeof(u8));

    spin_lock_init(&dev->lock);

    dev->queue = blk_init_queue(sbe_request, &dev->lock);
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
    if(dev->data) {
        vfree(dev->data);
    }
    return -EBUSY; // XXX: o.O
}


static int __init initialization_function(void) {
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
    init_device(devices, 0);

    printk(KERN_WARNING "SBE: init done\n");

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
        blk_cleanup_queue(dev->queue);
    }

    if(dev->data) {
        vfree(dev->data);
    }

    unregister_blkdev(sbe_major, "sbe");
    kfree(devices);

    printk(KERN_ALERT "Unloading module\n");
}

module_init(initialization_function);
module_exit(cleanup_function);
