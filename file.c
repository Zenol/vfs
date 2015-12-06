/*
 * sfs/file.c for SFS
 *
 * Copyright (C) 2009, Jeremy Cochoy
 */
#include <linux/buffer_head.h>
#include "sfs_fs.h"
#include "sfs.h"

int
sfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
  struct inode		*inode = dentry->d_inode;

  //Update block count
  inode->i_blocks = sfs_count_blocks(inode);
  //Fill attrs
  generic_fillattr(inode, stat);
  //Block size
  stat->blksize = dentry->d_parent->d_inode->i_sb->s_blocksize;

  return 0;
}

struct file_operations sfs_file_ops =
  {
    .llseek		= generic_file_llseek,
    .read		= do_sync_read,
    .aio_read		= generic_file_aio_read,
    .write		= do_sync_write,
    .aio_write		= generic_file_aio_write,
    .mmap		= generic_file_mmap,
    .splice_read	= generic_file_splice_read,
  };

struct inode_operations sfs_file_iops =
  {
    .truncate		= sfs_truncate,
    .getattr		= sfs_getattr,
  };
