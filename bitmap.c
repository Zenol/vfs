/*
 * sfs/bitmap.c for SFS
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

#define	BLOCK_BITMAP	0
#define	INODE_BITMAP	1

//Access at a map address
#define	map_addr(map, page, idx)	*((char*)(map)[(page)]->b_data + idx)
//Acess at a map block
#define	map_bh(page)			((map)[(page)])
//Select map mode
#define SEL_MAP(sbi, mode)		((mode == BLOCK_BITMAP) ? (sbi)->s_bmap : (sbi)->s_imap)
//Create map var
#define MAP(sbi, mode)			struct buffer_head **map = SEL_MAP(sbi, mode)

/**
 * sfs_get_bit - Get a bit from block bitmap or inode bitmap.
 * @sb SFS super block
 * @mode %BLOCK_BITMAP or %INODE_BITMAP
 *
 * Returns inode/block id or %-ENOSPC
 */
inline int
sfs_get_bit(struct super_block *sb, int mode)
{
  SBI(sb);
  MAP(sbi, mode);
  int	page;
  int	idx;
  int	off;
  int	lim_blocks = (mode == BLOCK_BITMAP) ? sbi->s_bmap_blocks : sbi->s_imap_blocks;
  int	lim_id = (mode == BLOCK_BITMAP) ? sbi->s_nblocks : sbi->s_ninodes;
  int	id;

  //Lock kernel during search
  //lock_kernel();
  mutex_lock(&sb->s_lock);

  //Search next unused byte
  for(page = 0; page < lim_blocks; page++)
    {
      //Until all bit are set, continue
      for(idx = 0;
	  idx < SFS_BLOCK_SIZE && !~map_addr(map, page, idx);
	  idx++);
      if(idx != SFS_BLOCK_SIZE)
	goto found;
    }

  //No inode/block left
  goto nospc_unlock;

  //inode/block found
 found:

  //Find offset
  off = 0;
  while ((1 << off) & map_addr(map, page, idx) && (off) < 8)
    off++;

  //Unlock kernel
  //unlock_kernel();
  mutex_unlock(&sb->s_lock);

  //Set bit and return inode ID
  if(off < 8)
    {
      //Get inode/block ID by uniting bytes
      id = (page << SFS_BLOCK_LOG_SIZE | idx << 3 | off);
      //Too Hight!
      printk(" ===>idx:%d off:%d\n", (int)idx, (int)off);
      printk(" ===>id:%d(page:%d) < lim:%d\n", (int)id, (int)page, (int)lim_id);
      if (id >= lim_id)
	goto nospc;
      //Set bit
      map_addr(map, page, idx) |= 1 << off;
      mark_buffer_dirty(map_bh(page));
      return id;
    }

  //Not enought space on disc
 nospc_unlock:
  //Unlock kernel
  //unlock_kernel();
  mutex_unlock(&sb->s_lock);
 nospc:
  return -ENOSPC;
}

/**
 * sfs_get_bit_after - Get a bit from block bitmap or inode bitmap after start.
 * @sb SFS super block
 * @mode %BLOCK_BITMAP or %INODE_BITMAP
 *
 * Returns inode/block id or %-ENOSPC
 */
inline int
sfs_get_bit_after(struct super_block *sb, int mode, unsigned long start)
{
  SBI(sb);
  MAP(sbi, mode);
  const unsigned int	start_page = start >> SFS_BLOCK_LOG_SIZE;
  const unsigned int	start_idx = (start & ~(start_page << SFS_BLOCK_LOG_SIZE)) >> 3;
  unsigned int		off = start & 0x07;
  unsigned int		idx = start_idx;
  unsigned int		page;
  int			lim_blocks = (mode == BLOCK_BITMAP) ? sbi->s_bmap_blocks : sbi->s_imap_blocks;
  int			lim_id = (mode == BLOCK_BITMAP) ? sbi->s_nblocks : sbi->s_ninodes;
  int			id;

  //Lock kernel during search
  //lock_kernel();
  mutex_lock(&sb->s_lock);

  //Search next unused byte
  for(page = start_page; page < lim_blocks; page++)
    {
      //Until all bit are set, continue
      for(;
	  idx < SFS_BLOCK_SIZE && !~map_addr(map, page, idx);
	  idx++);
      if(idx != SFS_BLOCK_SIZE)
	goto found;
      idx = 0;
    }

  //No inode/block left, try again from start
  //unlock_kernel();
  mutex_unlock(&sb->s_lock);
  goto restart;

  //inode/block found
 found:

  //Find offset
  if(page != start_page || idx != start_idx)
    off = 0;
  while ((1 << off) & map_addr(map, page, idx) && (off) < 8)
    off++;

  //Unlock kernel
  //unlock_kernel();
  mutex_unlock(&sb->s_lock);

  //Set bit and return inode ID
  if(off < 8)
    {
      //Get inode/block ID by uniting bytes
      id = (page << SFS_BLOCK_LOG_SIZE | idx << 3 | off);
      //Too Hight!
      printk(" ===>idx:%d off:%d\n", (int)idx, (int)off);
      printk(" ===>id:%d(page:%d) < lim:%d\n", (int)id, (int)page, (int)lim_id);
      if (id >= lim_id)
	goto restart;
      //Set bit
      map_addr(map, page, idx) |= 1 << off;
      mark_buffer_dirty(map_bh(page));
      return id;
    }

  //Not enought space on disk
 restart:
  return sfs_get_bit(sb, mode);
}

/**
 * sfs_put_bit - Free a inode/block by unseting bit in bitmap
 * @sb SFS super block
 * @id block/inode id
 * @mode %BLOCK_BITMAP or %INODE_BITMAP
 *
 * Returns 0 or %-INVAL
 */
int	sfs_put_bit(struct super_block *sb, unsigned long id, int mode)
{
  SBI(sb);
  MAP(sbi, mode);
  int	page;
  int	idx;
  int	off;
  int	lim_id = (mode == BLOCK_BITMAP) ? sbi->s_nblocks : sbi->s_ninodes;

  //Check id limit
  if(id >= lim_id)
    return -EINVAL;

  //Get page, index and offset
  page = id >> SFS_BLOCK_LOG_SIZE;
  off = id & 0x07;
  idx = (id & ~(page << SFS_BLOCK_LOG_SIZE)) >> 3;

  //Find offset or return error
  if (!((1 << off) & map_addr(map, page, idx)))
    {
      printk("  already unmaped id:%lu\n", id);
      return -EINVAL;
    }

  //Set bit and return 0
  map_addr(map, page, idx) &= ~(1 << off);
  mark_buffer_dirty(map_bh(page));
  printk(" ==put_bit==>id:%d(page:%d) < lim:%d\n", (int)id, (int)page, (int)lim_id);
  return 0;
}

/**
 * sfs_get_binode - Get a free inode and set bit in bitmap
 * @sb SFS super block
 *
 * Returns inode id or %-ENOSPC
 */
int	sfs_get_binode(struct super_block *sb)
{
  printk(" ===>inode\n");
  return sfs_get_bit(sb, INODE_BITMAP);
}

/**
 * sfs_get_bblock - Get a free block and set bit in bitmap
 * @sb SFS super block
 *
 * Returns block id or %-ENOSPC
 */
int	sfs_get_bblock(struct super_block *sb)
{
  printk(" ===>blk\n");
  return sfs_get_bit(sb, BLOCK_BITMAP);
}

/**
 * sfs_get_bblock_after - Get a free block after s@tart and set bit in bitmap
 * @sb SFS super block
 *
 * Returns block id or %-ENOSPC
 */
int	sfs_get_bblock_after(struct super_block *sb, unsigned long start)
{
  printk(" ===>blk\n");
  return sfs_get_bit_after(sb, BLOCK_BITMAP, start);
}

/**
 * sfs_put_binode - Free an inode by unseting bit in bitmap
 * @sb SFS super block
 *
 * Returns 0 or %-INVAL
 */
int	sfs_put_binode(struct super_block *sb, unsigned long ino)
{
  printk(" __=>bino\n");
  return sfs_put_bit(sb, ino, INODE_BITMAP);
}

/**
 * sfs_put_bblock - Get a free block and set bit in bitmap
 * @sb SFS super block
 *
 * Returns 0 or %-INVAL
 */
int	sfs_put_bblock(struct super_block *sb, unsigned long blk)
{
  printk(" __=>blk\n");
  return sfs_put_bit(sb, blk, BLOCK_BITMAP);
}
