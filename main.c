/*
 * main.c -- the bare mlbuf char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */

#include "mlbuf.h"		/* local definitions */

/*
 * Our parameters which can be set at load time.
 */

int mlbuf_major =   MLBUF;
int mlbuf_minor =   0;
int mlbuf_nr_devs = MLBUF_NR_DEVS;	/* number of bare mlbuf devices :*/
module_param(mlbuf_major, int, S_IRUGO);
module_param(mlbuf_nr_devs, int, S_IRUGO);

MODULE_AUTHOR("Yusuf Sahin");
MODULE_LICENSE("Dual BSD/GPL");

struct mlbuf_dev *mlbuf_devices;	/* allocated in mlbuf_init_module */
#define GET_SIZE(p, q) ((p) - (q) + 1)
#ifdef MLBUF_PROC /* use proc only if debugging */

/*
 * Setting up /proc.
 *
 * Functions needed to set up read-only entries in /proc with diagnostic
 * information on the various mlbuf devices.
 *
 * mlbufmem uses an older but common interface, which is awkward for creating
 * /proc entries greater than PAGE_SIZE bytes. mlbufseq implements the seq_file
 * interface, which is better-suited for large /proc entries.
 */

/* The mlbufmem proc implementation. */

int mlbuf_read_procmem(struct seq_file *s, void *v)
{
	int i, j;
	int limit = s->size - 80; /* Don't print more characters than this. */

	for (i = 0; i < mlbuf_nr_devs && s->count <= limit; i++) {
		struct mlbuf_dev *d = &mlbuf_devices[i];
		if (mutex_lock_interruptible(&d->mutex))
			return -ERESTARTSYS;
		/*  GENERATE PROC OUTPUT WITH seq_printf */
		mutex_unlock(&mlbuf_devices[i].mutex);
	}
        return 0;
}

static int mlbufmem_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, mlbuf_read_procmem, NULL);
}

struct file_operations mlbufmem_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = mlbufmem_proc_open,
	.llseek  = seq_lseek,
	.read    = seq_read,
	.release = single_release,
};

/* The mlbufseq proc implementation. */

static void *mlbuf_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= mlbuf_nr_devs)
		return NULL;   /* No more to read. */
	return mlbuf_devices + *pos;
}

static void *mlbuf_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= mlbuf_nr_devs)
		return NULL;
	return mlbuf_devices + *pos;
}

static void mlbuf_seq_stop(struct seq_file *s, void *v)
{
	/* There's nothing to do here! */
}

static int mlbuf_seq_show(struct seq_file *s, void *v)
{
	struct mlbuf_dev *dev = (struct mlbuf_dev *) v;
	struct mlbuf_qset *d;
	int i;
	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	/* OUTPUT DEVICE INFO */
	seq_printf(s, "\nDevice %i\n",
			(int) (dev - mlbuf_devices));
	mutex_unlock(&dev->mutex);
	return 0;
}
	
/*
 * Set up the sequence operator pointers.
 */
static struct seq_operations mlbuf_seq_ops = {
	.start = mlbuf_seq_start,
	.next  = mlbuf_seq_next,
	.stop  = mlbuf_seq_stop,
	.show  = mlbuf_seq_show
};

static int mlbufseq_proc_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &mlbuf_seq_ops);
}

static struct file_operations mlbufseq_proc_ops = {
	.owner   = THIS_MODULE,
	.open    = mlbufseq_proc_open,
	.llseek  = seq_lseek,
	.read    = seq_read,
	.release = seq_release,
};

/* Set up and remove the proc entries */

static void mlbuf_create_proc(void)
{
	proc_create_data("mlbufmem", 0 /* default mode */,
			NULL /* parent dir */, &mlbufmem_proc_ops,
			NULL /* client data */);
	proc_create_data("mlbufseq", 0, NULL, &mlbufseq_proc_ops, NULL);
}

static void mlbuf_remove_proc(void)
{
	/* No problem if it was not registered. */
	remove_proc_entry("mlbufmem", NULL /* parent dir */);
	remove_proc_entry("mlbufseq", NULL);
}

#endif /* MLBUF_PROC */


/*
 * Open and close
 */

int mlbuf_open(struct inode *inode, struct file *filp)
{
	struct mlbuf_dev *dev; /* device information */

	dev = container_of(inode->i_cdev, struct mlbuf_dev, cdev);
	filp->private_data = dev; /* for other methods */
	printk("mlbuf device %li opened\n",dev - mlbuf_devices);

	return 0;
}

int mlbuf_release(struct inode *inode, struct file *filp)
{
	printk("mlbuf device closed\n");
	return 0;
}

/*
 * Data management: read and write.
 */

ssize_t mlbuf_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct mlbuf_dev *dev = filp->private_data; 
	struct mesg_buf *pos, *n;
	int i;
	unsigned int buf_pos = 0;
	unsigned int bytes_left = count;
	unsigned char level_index;
	ssize_t retval = 0;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	printk("mlbuf device %li reading %li bytes\n",dev - mlbuf_devices, count);

	if (get_user(level_index, buf)) {
		retval = -EFAULT;
		goto out;
	}
	if(level_index == 0)
	{
		while(!(dev->sizes[0] >= count))
		{
			mutex_unlock(&dev->mutex);

			wait_event_interruptible(dev->que[0], (dev->sizes[0] < count));

			mutex_lock_interruptible(&dev->mutex);
		}
		for(i = 1; i < LEVELS; i++)
		{
			if(list_empty(&dev->levels[i].list) || dev->sizes[i] == 0)	
				continue;
			if(bytes_left > dev->sizes[i])
			{
				list_for_each_entry_safe(pos, n, &dev->levels[i].list, list){
						
					if(copy_to_user(buf+buf_pos, pos->data + pos->front, GET_SIZE(pos->size, pos->front)))
					{
						retval = -EFAULT;
						goto out;
					}
					dev->sizes[i] -= (GET_SIZE(pos->size, pos->front));
					dev->sizes[0] -= (GET_SIZE(pos->size, pos->front));
					buf_pos += (GET_SIZE(pos->size, pos->front));
					bytes_left -= (GET_SIZE(pos->size, pos->front));
					list_del(&pos->list);
					kfree(pos);
					if(bytes_left <= 0)
						break;
				}
			}
			else
			{
				list_for_each_entry_safe(pos, n, &dev->levels[i].list, list){
					if(bytes_left > 0)
					{
						if(bytes_left < (GET_SIZE(pos->size, pos->front)))
						{
							if(copy_to_user(buf+buf_pos, pos->data + pos->front, bytes_left))
							{
								retval = -EFAULT;
								goto out;
							}
							dev->sizes[i] -= bytes_left;
							dev->sizes[0] -= bytes_left;
							pos->front += bytes_left;
							bytes_left = 0;
						}
						else
						{
							if(copy_to_user(buf+buf_pos, pos->data + pos->front, GET_SIZE(pos->size, pos->front)))
							{
								retval = -EFAULT;
								goto out;
							}
							dev->sizes[i] -= (GET_SIZE(pos->size, pos->front));
							dev->sizes[0] -= (GET_SIZE(pos->size, pos->front));
							buf_pos += (GET_SIZE(pos->size, pos->front));
							bytes_left -= (GET_SIZE(pos->size, pos->front));
							list_del(&pos->list);
							kfree(pos);
						}
					}
				}
			}
		}
	}
	else
	{
		while(!(dev->sizes[level_index] >= count))
		{
			mutex_unlock(&dev->mutex);
			wait_event_interruptible(dev->que[level_index], (dev->sizes[level_index] < count));

			mutex_lock_interruptible(&dev->mutex);
		}
		list_for_each_entry_safe(pos, n, &dev->levels[level_index].list, list){
			if(bytes_left > 0)
			{
				if(bytes_left < (GET_SIZE(pos->size, pos->front)))
				{
					if(copy_to_user(buf+buf_pos, pos->data + pos->front, bytes_left))
					{
						retval = -EFAULT;
						goto out;
					}
					dev->sizes[level_index] -= bytes_left;
					dev->sizes[0] -= bytes_left;
					pos->front += bytes_left;
					bytes_left = 0;
				}
				else
				{
					if(copy_to_user(buf+buf_pos, pos->data + pos->front, GET_SIZE(pos->size, pos->front)))
					{
						retval = -EFAULT;
						goto out;
					}
					dev->sizes[level_index] -= (GET_SIZE(pos->size, pos->front));
					dev->sizes[0] -= (GET_SIZE(pos->size, pos->front));
					buf_pos += (GET_SIZE(pos->size, pos->front));
					bytes_left -= (GET_SIZE(pos->size, pos->front));
					list_del(&pos->list);
					kfree(pos);
				}
			}
			else
				break;
		}
	}
	/*
	if (copy_to_user(buf, buffer, count)) {
		retval = -EFAULT;
		goto out;
	}
	*/
	retval = count;

  out:
	mutex_unlock(&dev->mutex);
	return retval;
}

ssize_t mlbuf_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct mlbuf_dev *dev = filp->private_data;
	char user_buf[DATABUF_SIZE];
	unsigned char level_index;
	struct mesg_buf *pos, *new_node;
	int i, j, n_written;

	ssize_t retval = -ENOMEM; /* Value used in "goto out" statements. */
	memset(user_buf, 0, DATABUF_SIZE);

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	printk("mlbuf device %li writing %li bytes\n", dev - mlbuf_devices, count);

	if(count > DATABUF_SIZE || count <= 0) {
		retval = -EINVAL;
		goto out;
	}

	if (copy_from_user(user_buf, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	level_index = user_buf[0];
	if(level_index == 0)
	{
		retval = -EINVAL;
		goto out;
	}
	if(!list_empty(&dev->levels[level_index].list)){
		pos = list_last_entry(&dev->levels[level_index].list, struct mesg_buf,list);

		for(i = pos->size, n_written = 0; i < DATABUF_SIZE && n_written < count; i++, n_written++)
			pos->data[i] = user_buf[n_written];	

		pos->size += n_written;
		dev->sizes[level_index]+= n_written;
		dev->sizes[0] += n_written;
		if(n_written < count)
		{
			new_node = kmalloc(sizeof(struct mesg_buf), GFP_KERNEL);
			for(i = 0, j = n_written; i < (count - n_written); i++, j++)
				new_node->data[i] = user_buf[j];
			
			new_node->size = i;
			dev->sizes[level_index]+= i;
			dev->sizes[0] += i;
			list_add(&new_node->list, &pos->list);
		}

	}
	else {
			new_node = kmalloc(sizeof( struct mesg_buf), GFP_KERNEL);
			for(i = 0; i < count; i++)
				new_node->data[i] = user_buf[i];
			new_node->size = i;
			dev->sizes[level_index] += i;
			dev->sizes[0] += i;
			list_add(&new_node->list, &dev->levels[level_index].list);
	}
	wake_up_interruptible(&dev->que[level_index]);
	wake_up_interruptible(&dev->que[0]);
	retval = count;

  out:
	mutex_unlock(&dev->mutex);
	return retval;
}

/*
 * The ioctl() implementation.
 */

long mlbuf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int err = 0, tmp;
	int retval = 0;
	struct mesg_buf * pos, *n;

	struct mlbuf_dev *dev = filp->private_data;
	printk("mlbuf ioctl %i %li\n", cmd, arg);
    
	/*
	 * Extract the type and number bitfields, and don't decode incorrect
	 * cmds: return ENOTTY (inappropriate ioctl) before access_ok().
	 */
	if (_IOC_TYPE(cmd) != MLBUF_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > MLBUF_IOC_MAXNR)
		return -ENOTTY;

	/*
	 * The direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while access_ok is
	 * kernel-oriented, so the concept of "read" and "write" is reversed.
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;
	mutex_lock_interruptible(&dev->mutex);
	switch(cmd) {
	  case MLBUF_IOCCLR:
	  	for(tmp = 0; tmp < LEVELS; tmp++)
		{
			list_for_each_entry_safe(pos, n, &dev->levels[tmp].list, list)
			{
				list_del(&pos->list);
				kfree(pos);
			}
		}
		break;
        
	  case MLBUF_IOCHSIZE: /* Set: arg points to the value. */
	  	if(arg == -1)
		{
			//on 0th level i accumulated total size
			retval = dev->levels[0].size;
		}
		else
		{
			retval = dev->sizes[arg];
		}
		break;

	  default:  /* Redundant, as cmd was checked against MAXNR. */
		return -ENOTTY;
	}
	mutex_unlock(&dev->mutex);
	return retval;
}




struct file_operations mlbuf_fops = {
	.owner =    THIS_MODULE,
	.read =     mlbuf_read,
	.write =    mlbuf_write,
	.unlocked_ioctl = mlbuf_ioctl,
	.open =     mlbuf_open,
	.release =  mlbuf_release,
};

/*
 * Module setup.
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized.
 */
void mlbuf_cleanup_module(void)
{
	int i;
	dev_t devno = MKDEV(mlbuf_major, mlbuf_minor);

	/* Get rid of our char dev entries. */
	if (mlbuf_devices) {
		for (i = 0; i < mlbuf_nr_devs; i++) {
			cdev_del(&mlbuf_devices[i].cdev);
		}
		kfree(mlbuf_devices);
	}

#ifdef MLBUF_PROC /* Use proc only if debugging. */
	mlbuf_remove_proc();
#endif

	/* cleanup_module is never called if registering failed. */
	unregister_chrdev_region(devno, mlbuf_nr_devs);
}


/*
 * Set up the char_dev structure for this device.
 */
static void mlbuf_setup_cdev(struct mlbuf_dev *dev, int index)
{
	int err, devno = MKDEV(mlbuf_major, mlbuf_minor + index);
    
	cdev_init(&dev->cdev, &mlbuf_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &mlbuf_fops;
	err = cdev_add (&dev->cdev, devno, 1);
	/* Fail gracefully if need be. */
	if (err)
		printk(KERN_NOTICE "Error %d adding mlbuf%d", err, index);
}


int mlbuf_init_module(void)
{
	int result, i, j;
	dev_t dev = 0;
	/*
	 * Get a range of minor numbers to work with, asking for a dynamic major
	 * unless directed otherwise at load time.
	 */
	if (mlbuf_major) {
		dev = MKDEV(mlbuf_major, mlbuf_minor);
		result = register_chrdev_region(dev, mlbuf_nr_devs, "mlbuf");
	} else {
		result = alloc_chrdev_region(&dev, mlbuf_minor, mlbuf_nr_devs,
				"mlbuf");
		mlbuf_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "mlbuf: can't get major %d\n", mlbuf_major);
		return result;
	}

        /* 
	 * Allocate the devices. This must be dynamic as the device number can
	 * be specified at load time.
	 */
	mlbuf_devices = kmalloc(mlbuf_nr_devs * sizeof(struct mlbuf_dev), GFP_KERNEL);
	if (!mlbuf_devices) {
		result = -ENOMEM;
		goto fail;
	}
	memset(mlbuf_devices, 0, mlbuf_nr_devs * sizeof(struct mlbuf_dev));

        /* Initialize each device. */
	for (i = 0; i < mlbuf_nr_devs; i++) {
		mutex_init(&mlbuf_devices[i].mutex);
		mlbuf_setup_cdev(&mlbuf_devices[i], i);
		memset(mlbuf_devices->sizes, 0, LEVELS * sizeof(unsigned int));
		for(j = 0; j < LEVELS; j++)
		{
			INIT_LIST_HEAD(&mlbuf_devices[i].levels[j].list);
			init_waitqueue_head(&mlbuf_devices[i].que[j]);
			mlbuf_devices[i].levels[j].front = mlbuf_devices[i].levels[j].size = 0;
		}
	}

#ifdef MLBUF_PROC /* only when debugging */
	mlbuf_create_proc();
#endif

	return 0; /* succeed */

  fail:
	mlbuf_cleanup_module();
	return result;
}

module_init(mlbuf_init_module);
module_exit(mlbuf_cleanup_module);
