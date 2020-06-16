#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");


#define KERN_LOG_LEVEL		KERN_ALERT

#define BLOCK_MAJOR		240
#define BLKDEV_NAME		"mybdev"
#define BLOCK_MINORS		1
//initial size 100 Mb
#define NR_SECTORS		204800

#define KERNEL_SECTOR_SIZE	512


static struct block_dev {
	spinlock_t lock;
	struct request_queue *queue;
	struct gendisk *gd;
	u8 *data;
	size_t size;
} g_dev;

static int block_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

static void block_release(struct gendisk *gd, fmode_t mode)
{
}

static const struct block_device_operations block_ops = {
	.owner = THIS_MODULE,
	.open = block_open,
	.release = block_release
};

static void block_transfer(struct block_dev *dev, sector_t sector,
		unsigned long len, char *buffer, int dir)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;

	/* check for read/write beyond end of block device */
	if ((offset + len) > dev->size)
		return;

	/* read/write to dev buffer depending on dir */
	if (dir == 1)		/* write */
		memcpy(dev->data + offset, buffer, len);
	else
		memcpy(buffer, dev->data + offset, len);
}

static void block_request(struct request_queue *q)
{
	struct request *rq;
	struct block_dev *dev = q->queuedata;

	while (1) {

		/* fetch request */
		rq = blk_fetch_request(q);
		if (rq == NULL)
			break;

		/* print request information */
		printk(KERN_LOG_LEVEL
			"request received: pos=%llu bytes=%u "
			"cur_bytes=%u dir=%c\n",
			(unsigned long long) blk_rq_pos(rq),
			blk_rq_bytes(rq), blk_rq_cur_bytes(rq),
			rq_data_dir(rq) ? 'W' : 'R');

		/* process the request by calling block_transfer */
		block_transfer(dev, blk_rq_pos(rq),
				  blk_rq_bytes(rq),
				  bio_data(rq->bio), rq_data_dir(rq));

		/* end request successfully */
		__blk_end_request_all(rq, 0);
	}
}

static int create_block_device(struct block_dev *dev)
{
	int err;

	dev->size = NR_SECTORS * KERNEL_SECTOR_SIZE;
	dev->data = vmalloc(dev->size);
	if (dev->data == NULL) {
		printk(KERN_ERR "vmalloc: out of memory\n");
		err = -ENOMEM;
		goto out_vmalloc;
	}

	/* initialize the I/O queue */
	spin_lock_init(&dev->lock);
	dev->queue = blk_init_queue(block_request, &dev->lock);
	if (dev->queue == NULL) {
		printk(KERN_ERR "blk_init_queue: out of memory\n");
		err = -ENOMEM;
		goto out_blk_init;
	}
	blk_queue_logical_block_size(dev->queue, KERNEL_SECTOR_SIZE);
	dev->queue->queuedata = dev;

	/* initialize the gendisk structure */
	dev->gd = alloc_disk(BLOCK_MINORS);
	if (!dev->gd) {
		printk(KERN_ERR "alloc_disk: failure\n");
		err = -ENOMEM;
		goto out_alloc_disk;
	}

	dev->gd->major = BLOCK_MAJOR;
	dev->gd->first_minor = 0;
	dev->gd->fops = &block_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf(dev->gd->disk_name, DISK_NAME_LEN, "myblock");
	set_capacity(dev->gd, NR_SECTORS);

	add_disk(dev->gd);

    printk(KERN_INFO "create_block_device: successfully created\n");
	return 0;

out_alloc_disk:
	blk_cleanup_queue(dev->queue);
out_blk_init:
	vfree(dev->data);
out_vmalloc:
	return err;
}

static int __init block_init(void)
{
	int err = 0;

	/* register block device */
	err = register_blkdev(BLOCK_MAJOR, BLKDEV_NAME);
	if (err < 0) {
		printk(KERN_ERR "register_blkdev: unable to register\n");
		return err;
	}
    printk(KERN_INFO "register_blkdev: successfully registered\n");

	/* create block device using create_block_device */
	err = create_block_device(&g_dev);
	if (err < 0)
		goto out;

    printk(KERN_INFO "block_init: success\n");
	return 0;

out:
	/* unregister block device in case of an error */
	unregister_blkdev(BLOCK_MAJOR, BLKDEV_NAME);
    printk(KERN_ERR "register_blkdev: successfully unregistered\n");
	return err;
}

static void delete_block_device(struct block_dev *dev)
{
	if (dev->gd) {
		del_gendisk(dev->gd);
		put_disk(dev->gd);
	}
	if (dev->queue)
		blk_cleanup_queue(dev->queue);
	if (dev->data)
		vfree(dev->data);
    printk(KERN_INFO "delete_block_device: successfully deleted\n");
}

static void __exit block_exit(void)
{
	/* cleanup block device */
	delete_block_device(&g_dev);

	/* unregister block device */
	unregister_blkdev(BLOCK_MAJOR, BLKDEV_NAME);
    printk(KERN_INFO "block_exit: successfully unregistered\n");
}

module_init(block_init);
module_exit(block_exit);