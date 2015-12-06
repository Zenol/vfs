/*
 * sfs/super.c for SFS
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

/**
 * See sfs_find_block
 * @data : memory start of the first sfs_block_idx
 * @deph : where to locate (in deph[0]) the last index
 * @iblock : how much we had to walk
 * @blk : block found (or 0)
 * @count : number of sfs_block_idx
 */
static int
sfs_find_direct(struct sfs_block_idx *data, unsigned short *deph,
		sector_t *iblock, unsigned int *blk, unsigned int count)
{
  for (deph[0] = 0; deph[0] < count && data[deph[0]].b_start; deph[0] += 1)
    {
      //We find a block
      if (*iblock < data[deph[0]].b_count)
	{
	  *blk = *iblock + data[deph[0]].b_start;
	  return DEPH_DIRECT;
	}
      //Next block segment
      *iblock -= data[deph[0]].b_count;
    }
  return DEPH_NOTFOUND;
}

/**
 * See sfs_find_block
 * @deph : where to locate (in deph[1]) the last index
 * @iblock : how much we had to walk
 * @blk : block found (or 0)
 * @err : where to store error (might not be %NULL!)
 * @pos : physical indirect block location
 * @sb : filesystem's super_block
 */
static int
sfs_find_indirect(struct super_block *sb, __u32 pos, unsigned short *deph,
		  sector_t *iblock, unsigned int *blk, int *err)
{
  struct buffer_head	*bh;

  printk(KERN_DEBUG "  sfs_find_indirect\n");

  //Get block
  if (!(bh = sb_bread(sb, pos)))
    return (*err = -EIO);

  //Lock page
  printk("   locking page %p in find_indirect\n", bh->b_page); //Did it work??
  lock_page(bh->b_page);

  if(sfs_find_direct((struct sfs_block_idx*)bh->b_data,
  		     &deph[1], iblock, blk, INDIRECT_BY_BLOCK) == DEPH_DIRECT)
    return DEPH_INDIRECT;

  //Unlock page
  unlock_page(bh->b_page);

  //Release block
  mark_buffer_dirty(bh);
  brelse(bh);

  return DEPH_NOTFOUND;
}

/**
 * See sfs_find_block
 */
static int
sfs_find_dbindirect(struct super_block *sb, __u32 pos, unsigned short *deph,
		    sector_t *iblock, unsigned int *blk, int *err)
{
  struct buffer_head	*bh;
  int			ind_pos;
  int			res;

  printk(KERN_DEBUG "  sfs_find_indirect\n");

  //Get block
  if (!(bh = sb_bread(sb, pos)))
    return (*err = -EIO);

  //Lock page
  printk("   locking page %p in find_indirect\n", bh->b_page); //Did it work??
  lock_page(bh->b_page);

  for(deph[1] = 0; deph[1] < DBINDIRECT_BY_BLOCK; deph[1]++)
    {
      ind_pos = ((__u32*)bh->b_data)[deph[1]];
      res = sfs_find_dbdirect(sb, ind_pos, &deph[1], iblock, blk, err);
      if (res == DEPH_DIRECT)
	return DEPH_INDIRECT;
      if (deph[2] < INDIRECT_BY_BLOCK)
	return DEPH_NOTFOUND;
    }

  //Unlock page
  unlock_page(bh->b_page);

  //Release block
  mark_buffer_dirty(bh);
  brelse(bh);

  return DEPH_NOTFOUND;
}

/**
 * Find a block @iblock for an inode @inode, and store foudn position in deph.
 *
 * Store physical block in blk and return %DEPH_DIRECT, %DEPH_INDIRECT
 * or %DEPH_DBINDIRECT as the last deph searching.
 * if blk couldn't be found, set @blk to 0 and return %DEPH_NOTFOUND
 *
 * deph[0] store the position in ii->i_data tab.
 * deph[1] store :
 *                 * the index of sfs_block_idx from the start of the page
 *                 * if deph[2] is set; it's the location of the __u32
 *                   corresponding to the page where is located the sfs_block_idx
 * deph[2] store the position of sfs_block_idx in case of an dbindirect
 *
 * @inode the inode we are working on
 * @iblock the block number we search
 * @deph 3tab tab to localise direct/indirect/dbindirect position
 * @blk where to store the block found or 0
 * return the code coresponding to the event appened
 */
int	sfs_find_block(struct inode *inode, sector_t *iblock,
		       unsigned short *deph, unsigned int *blk)
{
  int	err = 0;
  struct sfs_inode_info *ii = sfs_i(inode);
  int	ret;
  //  const struct super_block *sb = inode->i_sb;


  printk("sfs_find_block\n");

  /// DIRECT BLOCK
  //We search in the first 4 field of ii->i_data
  if(sfs_find_direct((struct sfs_block_idx*)ii->i_data,
		     deph, iblock, blk, 4) == DEPH_DIRECT)
    return DEPH_DIRECT;
  deph[0] *= 2;
  //Don't exist
  if (deph[0] < 7)
    {
      //Store the last direct paire not empty
      deph[0] = (deph[0] > 0) ? deph[0] - 2 : 0;
      goto no_blk;
    }

  /// INDIRECT
  if(!ii->i_data)
    goto no_blk;
  if (sfs_find_indirect(inode->i_sb, ii->i_data[deph[0]], &deph[1],
			iblock, blk, &err))
    return DEPH_INDIRECT;
  if (err)
    return err;
  if (deph[1] < INDIRECT_BY_BLOCK)
    return DEPH_NOTFOUND;
  deph[0]++;

  /// DBINDIRECT
  if (!ii->i_data[deph[0]])
    goto no_blk;
  if ((ret = sfs_find_dbindirect(inode->i_sb, ii->i_data[deph[0]], &deph[1],
				 iblock, blk, &err)))
    return DEPH_DBINDIRECT;
  if (ret == DEPH_NOTFOUND)
    return DEPH_NOTFOUND;
  if (err)
    return err;

  return -ENOSPC;

 direct_blk:
  //Store block id
  return DEPH_DIRECT;

 no_blk:
  *blk = 0;
  return DEPH_NOTFOUND;
}

int
sfs_alloc_block(struct inode *inode,  sector_t *iblock,
		unsigned short *deph, unsigned int *blk)
{
  struct sfs_inode_info *ii = sfs_i(inode);
  int			err = -EIO;

  printk("sfs_alloc_block\n");

  //INDIRECT
  *blk = 0;
  printk("deph[0]:%d iblock:%lu\n", deph[0], (long unsigned int)*iblock);
  //While we had to add a block and it's still a direct
  for (;deph[0] < 7 && *iblock != (sector_t)-1; *iblock -= 1)
    {
      //Get the next free block (If deph[0] is empty, it's not a
      //      *blk = sfs_get_bblock_after(inode->i_sb, ii->i_data[deph[0]] + *iblock);
      *blk = sfs_get_bblock(inode->i_sb);
      printk("new_block_named :%u\n", *blk);
      //If dont exist, no spc
      if (*blk < 0)
      	{
	  err = -ENOSPC;
	  goto err;
      	}

      //blk is the next block after the inode's end : we merge it!
      if (*blk == ii->i_data[deph[0]] + ii->i_data[deph[0] + 1])
	{
	  printk("              = merge:%d\n", (int)ii->i_data[deph[0]]);
	  ii->i_data[deph[0] + 1]++;
	  continue;
	}

      //No block stored
      if (ii->i_data[deph[0]] == 0)
  	goto add_new_direct;

      //fragmented file! we go to the next entry!
      deph[0] += 2;
      //end of direct table [TODO : goto indirect!]
      if (deph[0] >= 8)
  	goto indirect;

      //add new inode
    add_new_direct:
      printk("              = add:%d\n", (int)*blk);
      ii->i_data[deph[0]] = *blk;
      ii->i_data[deph[0] + 1] = 1;
      continue;
    }

  //Block(s) added!
  if (*iblock == (sector_t)-1)
    return 0;

 indirect:
  goto err;

  //return error code
 err:
  printk("sfs_alloc_block err:%d\n", err);
  return err;
}
