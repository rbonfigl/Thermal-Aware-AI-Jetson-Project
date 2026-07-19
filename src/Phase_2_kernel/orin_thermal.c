#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/io.h>
#include <asm/page.h>
#include "orin_thermal_ioctl.h"


#define DRIVER_NAME "JETSON"
#define MINOR_COUNT 1
#define MEM_SIZE PAGE_SIZE
// Module metadata
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roman");
MODULE_DESCRIPTION("Phase 2: Orin Thermal Governor Character Device Skeleton");

static struct cdev jetson_thermal_cdev;
static char device_buffer[] = "42\n";
static dev_t dev_num;
static struct device *jetson_thermal_device;
static struct class *jetson_thermal_class;
static unsigned long thermal_dma_buf; //holds kernel virtual address

static int jetson_thermal_open(struct inode *inode, struct file *filp);
ssize_t jetson_thermal_read(struct file *filep, char __user *buf, size_t len, loff_t *f_pos);
static int jetson_thermal_release(struct inode *inodep, struct file *filep);
static long jetson_thermal_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int my_mmap(struct file *filep, struct vm_area_struct *vma);

static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = jetson_thermal_open,
    .read    = jetson_thermal_read,
    .release = jetson_thermal_release,
    .unlocked_ioctl = jetson_thermal_ioctl,
    .mmap = my_mmap,
};

static int __init  jetson_thermal_init(void) {
    int ret;
    struct thermal_telemetry *mmap_data;

    pr_info("%s :Orin Thermal Driver: Initialization started.\n", DRIVER_NAME);
    //allocating major num
    ret = alloc_chrdev_region(&dev_num, 0, MINOR_COUNT, DRIVER_NAME);
    if(ret < 0){
        pr_err("%s Failed to register major number\n", DRIVER_NAME);
        return -EFAULT;
    }

    //init cdev
    cdev_init(&jetson_thermal_cdev,&fops);
    jetson_thermal_cdev.owner = THIS_MODULE;
    ret = cdev_add(&jetson_thermal_cdev, dev_num, MINOR_COUNT);

    if(ret < 0){
        pr_err("%s cdev_add failed", DRIVER_NAME);
        return ret;
    }
    //create class
    jetson_thermal_class = class_create(THIS_MODULE,DRIVER_NAME);
    //if there is a error in creating the class delete and unregister major+minor
    if(IS_ERR(jetson_thermal_class)){
        pr_err("%s: Class create failed\n", DRIVER_NAME);
        cdev_del(&jetson_thermal_cdev);
        unregister_chrdev_region(dev_num,1);

        return PTR_ERR(jetson_thermal_class);
    }
    //create device
    jetson_thermal_device = device_create(jetson_thermal_class,NULL,dev_num,NULL,DRIVER_NAME);
    if(IS_ERR(jetson_thermal_device)){
        pr_err("%s: Device create failed\n", DRIVER_NAME);
        class_destroy(jetson_thermal_class);
        cdev_del(&jetson_thermal_cdev);
        unregister_chrdev_region(dev_num,1);
        return -1;
    }

    //mmap
    thermal_dma_buf = get_zeroed_page(GFP_KERNEL); //maps a contiguous physical block of memory and gives the virtual page in kernel
    if(!thermal_dma_buf){
        pr_err("%s: thermal_dma_buf failed to allocate\n", DRIVER_NAME);
        device_destroy(jetson_thermal_class, dev_num);
        class_destroy(jetson_thermal_class);
        cdev_del(&jetson_thermal_cdev);
        unregister_chrdev_region(dev_num, MINOR_COUNT);
        return -ENOMEM;
    }

    mmap_data = (struct thermal_telemetry *)thermal_dma_buf;
    mmap_data->temperature = 42;
    mmap_data->fsm_state = 0;

    return 0;
}

static void __exit jetson_thermal_exit(void) {
    device_destroy(jetson_thermal_class, dev_num);
    class_destroy(jetson_thermal_class);
    cdev_del(&jetson_thermal_cdev);
    unregister_chrdev_region(dev_num, MINOR_COUNT);
    free_page(thermal_dma_buf);
    pr_info("%s: Orin Thermal Driver: Safely unloaded.\n", DRIVER_NAME);
}

static int jetson_thermal_open(struct inode *inode, struct file *filp){
    pr_info("%s: Device open was success\n", DRIVER_NAME);
    return 0;
}

ssize_t jetson_thermal_read(struct file *filep, char __user *buf, size_t len, loff_t *f_pos){
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
        pr_err("%s: Failed to read\n", DRIVER_NAME);
        return -EFAULT;
    }
}
static int jetson_thermal_release(struct inode *inodep, struct file *filep){
    pr_info("%s: Device successfully closed\n", DRIVER_NAME);
    return 0;
}

//IOCTL
static long jetson_thermal_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    struct thermal_telemetry data;
    int write_data = 24;
    switch(cmd){
        case JETSON_THERMAL_RESET:
            pr_info("%s :Driver Reset\n", DRIVER_NAME);
            break;
        case JETSON_THERMAL_WRITE:
            if(copy_from_user(&write_data, (int __user *) arg,sizeof(write_data))){
                pr_err("%s Data write error\n", DRIVER_NAME);
                return -EFAULT;
            }
            break;
        case JETSON_THERMAL_READ:
            data.temperature = 42;
            data.fsm_state = 0;
            if(copy_to_user((struct thermal_telemetry __user *) arg, &data, sizeof(data))){
                pr_err("%s Data read error\n", DRIVER_NAME);
                return -EFAULT;
            }
            break;
        default:
            return -ENOTTY;
    }
    return 0;
}


static int my_mmap(struct file *filep, struct vm_area_struct *vma){
    int status;

    unsigned long pfn = virt_to_phys((void *)thermal_dma_buf) >> PAGE_SHIFT;

    unsigned long size = vma->vm_end - vma->vm_start;
    if (size > PAGE_SIZE) {
        return -EINVAL;
    }

    //create a virtual page in userspace mapped to the same physical page in RAM as the kernel so you can read the values
    status = remap_pfn_range(vma, vma->vm_start, pfn, vma->vm_end - vma->vm_start, vma->vm_page_prot);
    
    if(status){
        pr_err("%s :my_mmap - Error\n", DRIVER_NAME);
        return -EAGAIN;
    }
    return 0;
}

module_init(jetson_thermal_init);
module_exit(jetson_thermal_exit);