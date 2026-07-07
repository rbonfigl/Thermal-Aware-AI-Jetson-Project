#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

// Module metadata
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roman");
MODULE_DESCRIPTION("Phase 2: Orin Thermal Governor Character Device Skeleton");
MODULE_VERSION("0.1");

static int __init orin_thermal_init(void) {
    pr_info("Orin Thermal Driver: Initialization started.\n");
    return 0;
}

static void __exit orin_thermal_exit(void) {
    pr_info("Orin Thermal Driver: Safely unloaded.\n");
}

module_init(orin_thermal_init);
module_exit(orin_thermal_exit);