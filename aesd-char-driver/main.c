/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"

#include <linux/slab.h>

#define TEMP_BUFFER_SIZE 0

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Arslan Ahmad");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
char * temp_buffer = NULL;
size_t temp_buffer_size = 0;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *cb = &dev->cb;
    struct aesd_buffer_entry *dptr = NULL;
    size_t entry_offset_byte;
    size_t bytes_to_read;
    
    mutex_lock(&dev->lock);

    dptr = aesd_circular_buffer_find_entry_offset_for_fpos(cb, *f_pos, &entry_offset_byte);
    if (dptr == NULL){
        retval = 0;
        goto out;
    }

    bytes_to_read = dptr->size - entry_offset_byte;
    if (bytes_to_read > count)  bytes_to_read = count;

    if (copy_to_user(buf, dptr->buffptr + entry_offset_byte, bytes_to_read)){
        retval = -EFAULT;
        goto out;
    }

    *f_pos += bytes_to_read;
    retval = bytes_to_read;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    int res;
    int bytes_scanned = 0;
    struct aesd_dev *dev = filp ->private_data;
    res = mutex_lock_interruptible(&dev->lock);
    if (res) return -ERESTARTSYS;

    char *new_data = (char *)kmalloc(count, GFP_KERNEL);
    if (new_data == NULL) goto out;

    memset(new_data, 0, count);
    res = copy_from_user(new_data, buf, count);

    while (bytes_scanned <= (count -1)){
        temp_buffer = krealloc(temp_buffer, ++temp_buffer_size, GFP_KERNEL);
        if (!temp_buffer){
            retval = -ENOMEM;
            goto out;
        }
        temp_buffer[temp_buffer_size-1] = new_data[bytes_scanned];

        if (new_data[bytes_scanned] == '\n'){
            struct aesd_buffer_entry new_entry;
            new_entry.buffptr = (char *)kmalloc(temp_buffer_size, GFP_KERNEL);
            new_entry.size = temp_buffer_size;
            memcpy(new_entry.buffptr, temp_buffer, temp_buffer_size);

            if (dev->cb.full){
                if (dev->cb.entry[dev->cb.in_offs].buffptr) kfree(dev->cb.entry[dev->cb.in_offs].buffptr);
            }

            aesd_circular_buffer_add_entry(&dev->cb, &new_entry);

            temp_buffer = krealloc(temp_buffer, 0, GFP_KERNEL);
            temp_buffer_size = 0;
        }
        bytes_scanned++;
    }

    retval = bytes_scanned;
    *f_pos += bytes_scanned;

    
out:
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    loff_t cbuf_size;

    uint8_t i;
    struct aesd_buffer_entry *entry;
    if(mutex_lock_interruptible(&dev->lock)) return -ERESTARTSYS;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->cb, i) 
    {
        cbuf_size += entry->size;
    }
    mutex_unlock(&dev->lock);

    return fixed_size_llseek(filp, off, whence, cbuf_size);
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    loff_t cbuf_size, new_fpos = 0;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    // we only support one command, so if there is a different command, return error.
    if (cmd != AESDCHAR_IOCSEEKTO) return -EINVAL;

    if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0){
        return -EFAULT;
    }

    if (seekto.write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) return -EINVAL;

    uint8_t i;
    struct aesd_buffer_entry *entry;
    if(mutex_lock_interruptible(&dev->lock)) return -ERESTARTSYS;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->cb, i) 
    {
        cbuf_size += entry->size;
        if (i < seekto.write_cmd) new_fpos += entry->size;
    }
    mutex_unlock(&dev->lock);

    if (new_fpos > cbuf_size) return -EINVAL;
    new_fpos += seekto.write_cmd_offset;
    filp->f_pos = new_fpos;

    return 0;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.cb);
    temp_buffer = (char *)kmalloc(TEMP_BUFFER_SIZE, GFP_KERNEL);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    uint8_t index;
    struct aesd_buffer_entry *entry;

    if (temp_buffer) kfree(temp_buffer);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.cb, index){
        if (entry->buffptr) {
            kfree (entry->buffptr);
            entry->buffptr = NULL;
        }
    }

    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
