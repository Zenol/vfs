/*
 * sfs/symlink.c for SFS
 *
 * Copyright (C) 2009, Jeremy Cochoy
 */
#include <linux/buffer_head.h>
#include "sfs_fs.h"
#include "sfs.h"

struct inode_operations sfs_symlink_iops =
  {
    .readlink	= generic_readlink,
    .follow_link	= page_follow_link_light,
    .put_link	= page_put_link,
    .getattr		= sfs_getattr,
  };
