/*
 * sfs/inode.c for SFS
 *
 * Copyright (C) 2009, Jeremy Cochoy
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "sfs_fs.h"
#include "sfs.h"

struct sfs_inode*
sfs_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh)
{
  struct sfs_inode	*iraw;
  SBI(sb);

  printk(KERN_DEBUG " sfs_raw_inode %d\n", (int)ino);

  //Check range
  if (ino >= sbi->s_ninodes)
    {
      printk("SFS-fs warning: inode %ld out of range\n", ino);
      return NULL;
    }
  //Get the block where is located inode
  *bh = sb_bread(sb, ino / INODE_PER_BLOCK + sbi->s_firstinodeblock);

  //Can't read
  if (!*bh)
    {
      printk("SFS-fs warning: can't read inode's block\n");
      return NULL;
    }

  //Return ptr to inode's data
  iraw = (void*)(*bh)->b_data;
  return iraw + ino % INODE_PER_BLOCK;
}

/**
 * sfs_set_inode_ops - Select inode_ops and file_ops from i_mode
 * @inode An inode to set
 * @rdev Special device to link
 */
void
sfs_set_inode_ops(struct inode *inode, dev_t rdev)
{
  //File
  if (S_ISREG(inode->i_mode))
    {
      inode->i_op = &sfs_file_iops;
      inode->i_fop = &sfs_file_ops;
      inode->i_mapping->a_ops = &sfs_address_space_ops;
      printk(KERN_DEBUG " - FILE\n");
    }
  //Directory
  else if(S_ISDIR(inode->i_mode))
    {
      inode->i_fop = &sfs_dir_ops;
      inode->i_op = &sfs_dir_iops;
      inode->i_mapping->a_ops = &sfs_address_space_ops;
      printk(KERN_DEBUG " - DIR\n");
    }
  //SymLink
  else if (S_ISLNK(inode->i_mode))
    {
      inode->i_op = &sfs_symlink_iops;
      inode->i_mapping->a_ops = &sfs_address_space_ops;
    }
  //Special device (ex: mount point)
  else
    init_special_inode(inode, inode->i_mode, rdev);
}

/**
 * sfs_new_inode - Create and allocate a new inode
 * @sb SFS super block
 *
 * Returns a new inode ptr or an error code (use IS_ERR)
 */
struct inode*
sfs_new_inode(struct super_block *sb)
{
  struct inode		*inode;
  struct sfs_inode_info	*iinode;
  int			ino;

  printk(KERN_DEBUG "sfs_new_inode\n");

  //Try to get an inode
  ino = sfs_get_binode(sb);
  if (IS_ERR(ERR_PTR(ino)))
  return ERR_PTR(ino);

  //Alocate it
  inode = new_inode(sb);
  if(!inode)
    return ERR_PTR(-ENOMEM);

  //Set default value (like time etc...)
  inode->i_uid = current_fsuid();
  inode->i_gid = current_fsgid();
  inode->i_ino = ino;
  inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
  iinode = sfs_i(inode);
  memset(iinode->i_data, 0, sizeof(iinode->i_data));

  //Hash and save
  insert_inode_hash(inode);
  mark_inode_dirty(inode);

  //Return the new inode
  return inode;
}

void
sfs_truncate(struct inode *inode)
{
  int			i, j;
  struct sfs_inode_info	*sii = sfs_i(inode);

  printk(KERN_DEBUG "  sfs_truncate\n");

  //Truncate page after new inode's size
  block_truncate_page(inode->i_mapping, inode->i_size, sfs_get_block);

  inode->i_blocks = sfs_count_blocks(inode);
  //Can't handle indirect and double indirect
  if(inode->i_blocks > 7)
    {
      //For each direct tuple
      for (i = inode->i_blocks * 2;
	   i < 8 * 2;
	   i += 2)
	//If there are blocks (block_id and block_count)
	if (sii->i_data[i] && sii->i_data[i + 1])
	  {
	    //Unmap all blocks
	    for(j = 0;
		j < sii->i_data[i+1];
		j++)
	      sfs_put_bblock(inode->i_sb, sii->i_data[i] + j);
	    //Remove datas
	    sii->i_data[i] = 0;
	    sii->i_data[i+1] = 0;
	  }
    }
}
