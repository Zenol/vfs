/*
 * sfs/adspace.c for SFS
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

//Associate logical block to physical block
int sfs_get_block
(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
  unsigned short 	deph[3];
  int			err = -EIO;
  unsigned int		blk = 0;

  printk("sfs_get_block = %d (create:%d)\n", (int)iblock, (int)create);

  //Invalid negativ block number!
  if (iblock < 0)
    {
      printk("SFS-warning: In get_block, invalid iblock = %d\n", (int)iblock);
      return err;
    }

  //Error when searching?
  if((err = sfs_find_block(inode, &iblock, deph, &blk)) < 0)
    goto err;
  //Block found
  if (blk)
    goto map;

  //Generate new block
  if((err = sfs_alloc_block(inode, &iblock, deph, &blk) < 0))
    goto err;

  //Map block into bh
 map:
  printk(" gblock map : %d\n", blk);
  map_bh(bh_result, inode->i_sb, blk);
  //All it's ok, update i_blocks
  inode->i_blocks = sfs_count_blocks(inode);
  return 0;

 err:
  return err;
}

//Read page with sfs_get_block
static int sfs_readpage
(struct file *file, struct page *page)
{
  printk(KERN_DEBUG "sfs_readpage\n");
  return block_read_full_page(page, sfs_get_block);
}

//Write page with sfs_get_block
static int sfs_writepage
(struct page *page, struct writeback_control *wbc)
{
  printk(KERN_DEBUG "sfs_writepage\n");
  return block_write_full_page(page, sfs_get_block, wbc);
}

//Prepare Write page
int __sfs_write_begin
(struct file *file, struct address_space *mapping,
 loff_t pos, unsigned len, unsigned flags,
 struct page **pagep, void **fsdata)
{
  printk(KERN_DEBUG "__sfs_write_begin\n");
  return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata, sfs_get_block);
}

//Prepare Write page with sfs_get_block
static int sfs_write_begin
(struct file *file, struct address_space *mapping,
 loff_t pos, unsigned len, unsigned flags,
 struct page **pagep, void **fsdata)
{
  printk(KERN_DEBUG "sfs_write_begin\n");
  //Called by kernel, *pagep can be uninitialised!
  *pagep = NULL;
  return block_write_begin(file, mapping, pos, len, flags, pagep, fsdata, sfs_get_block);
}

//BMAP with sfs_get_block
static sector_t sfs_bmap(struct address_space *mapping, sector_t block)
{
  printk(KERN_DEBUG "sfs_bmap\n");
  return generic_block_bmap(mapping, block, sfs_get_block);
}

//Maping virtual connex memory of a file into physical blocks
struct address_space_operations sfs_address_space_ops =
  {
    .readpage = sfs_readpage,
    .writepage = sfs_writepage,
    .sync_page = block_sync_page,
    .write_begin = sfs_write_begin,
    .write_end = generic_write_end,
    .bmap = sfs_bmap,
  };

//Release page
void
sfs_put_page(struct page* page)
{
  kunmap(page);
  page_cache_release(page);
}

//Get page
struct page*
sfs_get_page(struct inode *inode, unsigned long index)
{
  struct page *page;

  page = read_mapping_page(inode->i_mapping, index, NULL);
  if (!IS_ERR(page))
    {
      kmap(page);
      if (!PageUptodate(page))
	{
	  sfs_put_page(page);
	  return ERR_PTR(-EIO);
	}
    }
  return page;
}
