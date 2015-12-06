#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = KBUILD_MODNAME,
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
 .arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x1e14dc85, "module_layout" },
	{ 0xbb2f5218, "sb_set_blocksize" },
	{ 0x8f002838, "get_sb_bdev" },
	{ 0xe3f4d45f, "kill_block_super" },
	{ 0xd18cf8d5, "inode_init_once" },
	{ 0x9055334c, "register_filesystem" },
	{ 0xf73d3338, "kmem_cache_create" },
	{ 0x7a4507b5, "unregister_filesystem" },
	{ 0x921083b0, "kmem_cache_destroy" },
	{ 0xb72397d5, "printk" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

