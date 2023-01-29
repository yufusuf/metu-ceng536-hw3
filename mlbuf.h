
#ifndef _MLBUF_H_
#define _MLBUF_H_

/* Gives us access to ioctl macros _IO and friends below. */
#include <linux/ioctl.h>
#include <linux/mutex.h>

#define DATABUF_SIZE 1024
#define LEVELS 256
struct mesg_buf {
	struct list_head list;
	char data[DATABUF_SIZE];
	unsigned int size; // back pointer
	unsigned int front;
};
struct mlbuf_dev {
	struct mutex mutex;
	unsigned long size;       /* Amount of data stored here. */
	struct cdev cdev;	  /* Char device structure. */
	struct mesg_buf levels[LEVELS];
	wait_queue_head_t que[LEVELS];
	unsigned int sizes[LEVELS];
};


/*
 * The different configurable parameters
 */
extern int mlbuf_major;     /* main.c */
extern int mlbuf_nr_devs;

#define MLBUF	144
#define MLBUF_NR_DEVS	4

#define MLBUF_IOC_MAGIC  222


/* Use 222 as magic number */
#define MLBUF_IOC_MAGIC  222
/* Please use a different 8-bit number in your code */


/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define MLBUF_IOCCLR    _IO(MLBUF_IOC_MAGIC, 0)
#define MLBUF_IOCHSIZE _IOR(MLBUF_IOC_MAGIC,  1, int)

#define MLBUF_IOC_MAXNR 1

#endif /* _MLBUF_H_ */
