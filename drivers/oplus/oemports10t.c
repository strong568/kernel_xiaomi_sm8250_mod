// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>

#define LINK_DIR "oemports10t"
#define LINK_FILE "danda"
#define LINK_TARGET "/vendor/build.prop"

static struct proc_dir_entry *oem_link_dir;
static struct proc_dir_entry *oem_link_file;

static int __init oemports10t_init(void)
{
	oem_link_dir = proc_mkdir(LINK_DIR, NULL);
	if (!oem_link_dir) {
		pr_err("oemports10t: failed to create proc dir %s\n", LINK_DIR);
		return -ENOMEM;
	}

	oem_link_file = proc_symlink(LINK_FILE, oem_link_dir, LINK_TARGET);
	if (!oem_link_file) {
		pr_err("oemports10t: failed to create symlink %s -> %s\n", LINK_FILE, LINK_TARGET);
		remove_proc_entry(LINK_DIR, NULL);
		return -ENOMEM;
	}

	pr_info("oemports10t: initialized /proc/%s/%s -> %s\n", LINK_DIR, LINK_FILE, LINK_TARGET);
	return 0;
}

static void __exit oemports10t_exit(void)
{
	remove_proc_entry(LINK_FILE, oem_link_dir);
	remove_proc_entry(LINK_DIR, NULL);
	pr_info("oemports10t: unloaded\n");
}

module_init(oemports10t_init);
module_exit(oemports10t_exit);

MODULE_DESCRIPTION("OEM ports 10T helper");
MODULE_LICENSE("GPL");