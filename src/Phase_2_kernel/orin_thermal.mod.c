#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x25f8bfc1, "module_layout" },
	{ 0x4302d0eb, "free_pages" },
	{ 0x8cb3f82a, "class_destroy" },
	{ 0xcd9f81af, "device_destroy" },
	{ 0xf09b5d9a, "get_zeroed_page" },
	{ 0x396ecf2f, "device_create" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x277c232f, "cdev_del" },
	{ 0xeab3563e, "__class_create" },
	{ 0xd5a0871d, "cdev_add" },
	{ 0x5d7a3971, "cdev_init" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x3ea1b6e4, "__stack_chk_fail" },
	{ 0xdcb764ad, "memset" },
	{ 0x12a4e128, "__arch_copy_from_user" },
	{ 0x908e5601, "cpu_hwcaps" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0x69f38847, "cpu_hwcap_keys" },
	{ 0x14b89635, "arm64_const_caps_ready" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xe4bbc1dd, "kimage_voffset" },
	{ 0xccf30285, "remap_pfn_range" },
	{ 0x9688de8b, "memstart_addr" },
	{ 0x4b0a3f52, "gic_nonsecure_priorities" },
	{ 0x92997ed8, "_printk" },
	{ 0x1fdc7df2, "_mcount" },
};

MODULE_INFO(depends, "");

