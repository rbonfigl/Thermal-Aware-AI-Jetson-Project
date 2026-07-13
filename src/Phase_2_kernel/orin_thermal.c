#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>


#define DEVICE_NAME "JETSON"
#define MINOR_COUNT 1
// Module metadata
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roman");
MODULE_DESCRIPTION("Phase 2: Orin Thermal Governor Character Device Skeleton");

static struct cdev jetson_thermal_cdev;
static char device_buffer[] = "42\n";
static dev_t dev_num;

static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = jetson_thermal_open,
    .read    = jetson_thermal_read,
    .release = jetson_thermal_release,
};

static int __init  jetson_thermal_init(void) {
    pr_info("%s :Orin Thermal Driver: Initialization started.\n", DEVICE_NAME);
    int ret;
    //allocating major num
    ret = alloc_chrdev_region(&dev_num, 0, MINOR_COUNT, DEVICE_NAME);
    if(ret < 0){
        pr_err("%s Failed to register major number\n", DEVICE_NAME);
        return -EFAULT;
    }

    //init cdev
    cdev_init(&jetson_thermal_cdev,&fops);
    jetson_thermal_cdev.owner = THIS_MODULE;
    ret = cdev_add(&jetson_thermal_cdev, dev_num, MINOR_COUNT);

    if(ret < 0){
        pr_err("%s cdev_add failed", DEVICE_NAME);
        return ret;
    }
    //create class
    
    //create device

    return 0;
}

static void __exit jetson_thermal_exit(void) {
    pr_info("Orin Thermal Driver: Safely unloaded.\n");
}

static int jetson_thermal_open(struct inode *inode, struct file *filp){
    pr_info("%s: Device open was success\n", DEVICE_NAME);
    return 0;
}

ssize_t jetson_thermal_read(struct file *filep, char __user *buf, size_t len, loff_t *f_pos){
    size_t retval = 0;
    size_t error_count = 0;

    if(*f_pos >= sizeof(device_buffer)){ //if we are at or past end of file
        return 0;
    }
    if(len > sizeof(device_buffer)- *f_pos){ //length of message is greater then what we have left
        len = sizeof(device_buffer)-*f_pos;
    }

    error_count = copy_to_user(buf, device_buffer + *f_pos, len);

    if(error_count == 0){
        *f_pos += len;
        return len; //number of bytes read by userspace
    }
    else{
        pr_err("%s: Failed to read\n", DEVICE_NAME);
        return -EFAULT;
    }
}
static int jetson_thermal_release(struct inode *inodep, struct file *filep){
    pr_info("%s: Device successfully closed\n", DEVICE_NAME);
    return 0;
}

module_init(jetson_thermal_init);
module_exit(jetson_thermal_exit);