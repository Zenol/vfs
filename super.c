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

/*
** *********
** * CACHE *
** *********
*/
struct kmem_cache *sfs_inode_cache = NULL;

/*
** *************
** * SUPER OPS *
** *************
*/
struct inode*
sfs_alloc_inode(struct super_block *sb)
{
  struct sfs_inode_info	*ii;

  printk(KERN_DEBUG "SFS: alloc_inode\n");
  if (!(ii = kmem_cache_alloc(sfs_inode_cache, GFP_KERNEL)))
    return NULL;
  return &ii->vfs_inode;
}

static void
sfs_destroy_inode(struct inode *inode)
{
  printk(KERN_DEBUG "SFS: destroy_inode\n");

  kmem_cache_free(sfs_inode_cache, inode);
}

static void
sfs_put_super(struct super_block *sb)
{
  int				map_blocks;
  int				i;
  struct buffer_head		**map;
  struct sfs_super_block	*hsb;
  SBI(sb);

  printk(KERN_DEBUG "SFS: put_super\n");

  //Free MAPS
  map = sbi->s_imap;
  map_blocks = sbi->s_imap_blocks + sbi->s_imap_blocks;
  if (map)
    for(i = 0; i < map_blocks && map[i]; i++)
      brelse(map[i]);
  kfree(map);

  //Release SB
  hsb = sfs_sb(sb);
  hsb->s_state &= ~SFS_MOUNTED;
  mark_buffer_dirty(sbi->s_bh);
  brelse(sbi->s_bh);

  //Remove SBI
  STORE_SBI(sb, NULL);
  kfree(sbi);
}

static struct buffer_head*
sfs_update_inode(struct inode *inode)
{
  struct buffer_head	*bh;
  struct sfs_inode	*iraw;
  struct sfs_inode_info	*ii;
  int			i;

  if (!(iraw = sfs_raw_inode(inode->i_sb, inode->i_ino, &bh)))
    return NULL;
  iraw->i_mode = inode->i_mode;
  iraw->i_nlink = inode->i_nlink;
  iraw->i_uid = inode->i_uid;
  iraw->i_gid = inode->i_gid;
  iraw->i_atime = inode->i_atime.tv_sec;
  iraw->i_mtime = inode->i_mtime.tv_sec;
  iraw->i_ctime = inode->i_ctime.tv_sec;
  ii = sfs_i(inode);
  iraw->i_size = inode->i_size;
  inode->i_blocks = sfs_count_blocks(inode);
  for(i = 0; i < INO_DATA_COUNT; i++)
    iraw->i_data[i] = ii->i_data[i];
  mark_buffer_dirty(bh);
  return bh;
}

static int
sfs_write_inode(struct inode *inode, int wait)
{
  struct buffer_head	*bh;

  printk(KERN_DEBUG "     : write inode %ld\n", inode->i_ino);
  bh = sfs_update_inode(inode);
  brelse(bh);

  return 0;
}
static void
sfs_delete_inode(struct inode *inode)
{
  printk(KERN_DEBUG "     : delete inode %ld\n", inode->i_ino);
  truncate_inode_pages(&inode->i_data, 0);

  //Truncate inode
  i_size_write(inode, 0);
  sfs_truncate(inode);

  //Free inode's bit in bitmap
  sfs_put_binode(inode->i_sb, inode->i_ino);

  clear_inode(inode);
}

/*
** ***************
** * MODULE DATA *
** ***************
*/
//FS type
static struct file_system_type sfs_fs_type =
  {
    .name	= "sfs",
    .get_sb	= sfs_get_sb, //Get superblock (mount)
    .kill_sb	= sfs_kill_sb, //Kill superblock (umount)
    .owner	= THIS_MODULE, //The filesystem owner(THIS_MODULE)
    .fs_flags	= FS_REQUIRES_DEV, //Need a block device to work'on
  };
//Super operations
static struct super_operations sfs_super_operations =
  {
    .alloc_inode	= sfs_alloc_inode, //Allocate inode's memory
    .destroy_inode	= sfs_destroy_inode, //Free inode's memory
    .write_inode	= sfs_write_inode, //Write inode's data
    .delete_inode	= sfs_delete_inode, //LOG Inode Destruction
    .put_super		= sfs_put_super, //Unmount
    .statfs		= simple_statfs/* simple_statfs */,
    .remount_fs		= NULL,
  };

/*
** *************************
** * MODULE IMPLEMENTATION *
** *************************
*/

//Create a VFS inode
struct inode	*sfs_iget(struct super_block *sb, ino_t ino)
{
  struct inode		*inode;
  struct sfs_inode	*iraw;
  struct sfs_inode_info	*sii;
  struct buffer_head	*bh;
  int			i;

  printk(KERN_DEBUG "SFS: iget %ld\n", ino);

  inode = iget_locked(sb, ino);

  //Can't alloc
  if (!inode)
    return ERR_PTR(-ENOMEM);

  //Old inode
  if (!(inode->i_state & I_NEW))
    return inode;

  //Read from disk and link data
  iraw = sfs_raw_inode(sb, ino, &bh);
  if (!iraw)
    {
      iget_failed(inode);
      return ERR_PTR(-EIO);
    }

  //Copy data to memory from disk
  inode->i_mode = iraw->i_mode;
  inode->i_nlink = iraw->i_nlink;
  inode->i_uid = iraw->i_uid;
  inode->i_gid = iraw->i_gid;
  inode->i_atime.tv_sec = iraw->i_atime;
  inode->i_mtime.tv_sec = iraw->i_mtime;
  inode->i_ctime.tv_sec = iraw->i_ctime;
  sii = sfs_i(inode);
  for(i = 0; i < INO_DATA_COUNT; i++)
    sii->i_data[i] = iraw->i_data[i];
  inode->i_size = iraw->i_size;
  inode->i_blocks = sfs_count_blocks(inode);
  //Select OPS from type
  sfs_set_inode_ops(inode, 0);
  printk("  ok nlink %d\n", inode->i_nlink);
  printk("  ok mod %x\n", inode->i_mode);
  brelse(bh);

  //Unlock inode, and let's go!
  unlock_new_inode(inode);
  return inode;
}

//Fill a superblock
static int
sfs_fill_super(struct super_block *sb, void *data, int silent)
{
  //Default return
  int				 ret = -EINVAL;
  //Root inode
  struct inode			*iroot;
  //Block Head
  struct buffer_head		*bh;
  //SFS  Super Block
  struct sfs_super_block	*ssb;
  //SB Info
  struct sfs_sb_info		*sbi;
  //Map (imap & bmap)
  struct buffer_head		**map;
  //Indexs
  int				block = 0, i = 0;

  //DBG MSG
  printk(KERN_DEBUG "SFS: fill_super\n");

  //Check arch compatibility
  BUILD_BUG_ON(32 != sizeof(struct sfs_super_block));
  BUILD_BUG_ON(64 != sizeof(struct sfs_inode));
  BUILD_BUG_ON(8  != sizeof(struct sfs_block_idx));

  //Allocate sb info
  sbi = kzalloc(sizeof(struct sfs_sb_info), GFP_KERNEL);
  if(!sbi)
    return -ENOMEM;
  STORE_SBI(sb, sbi);

  //Initialise SuperBlock
  if(!sb_set_blocksize(sb, SFS_BLOCK_SIZE))
    goto out_bad_blocksize;

  //Read superblock
  if (!(bh = sb_bread(sb, 0)))
    goto out_no_sb;

  ssb = (void*)bh->b_data;
  sbi->s_bh = bh;

  if (!(ssb->s_magic == SFS_MAGIC))
    goto out_bad_magic;

  sb->s_magic = ssb->s_magic;
  sbi->s_nblocks = ssb->s_nblocks;
  sbi->s_ninodes = ssb->s_ninodes;
  sbi->s_inode_blocks = ssb->s_inode_blocks;
  sbi->s_imap_blocks = ssb->s_imap_blocks;
  sbi->s_bmap_blocks = ssb->s_bmap_blocks;
  sbi->s_firstinodeblock = ssb->s_imap_blocks + ssb->s_bmap_blocks + 1;
  sbi->s_firstdatablock = ssb->s_firstdatablock;
  sbi->s_state = ssb->s_state;
  sbi->s_namelen = ssb->s_namelen;

  //Check validity
  if (!(ssb->s_state & SFS_VALID_FS))
    {
      printk("SFS-fs warning: mounting an inalid file system, "
	     "running fsck is recommended\n");
    }
  else if (ssb->s_state & SFS_MOUNTED)
    {
      printk("SFS-fs warning: mounting an incorrectly unmounted file system, "
	     "running fsck is recommended\n");
      if (!(sb->s_flags & MS_RDONLY))
	ssb->s_state &= ~SFS_VALID_FS;
    }

  //Not read-only
  if ((!sb->s_flags & MS_RDONLY))
    {
      ssb->s_state |= SFS_MOUNTED;
      mark_buffer_dirty(bh);
    }

  //Create map table
  map = kmalloc((sbi->s_imap_blocks + sbi->s_bmap_blocks) * sizeof(bh),
		GFP_KERNEL);
  if (!map)
    goto out_no_map;
  sbi->s_imap = map;
  sbi->s_bmap = map + sbi->s_imap_blocks;

  block = 1;
  for (i = 0; i < sbi->s_imap_blocks; i++)
    {
      if (!(sbi->s_imap[i] = sb_bread(sb, block)))
	goto out_err_map;
      ++block;
    }
  for (i = 0; i < sbi->s_bmap_blocks; i++)
    {
      if (!(sbi->s_bmap[i] = sb_bread(sb, block)))
	goto out_err_map;
      ++block;
    }

  //Link operation table
  sb->s_op = &sfs_super_operations;

  //Get root inode
  iroot = sfs_iget(sb, SFS_ROOT_INO);

  //Check inode's data
  if(IS_ERR(iroot))
    goto out_no_iroot;
  if (!(iroot->i_mode & S_IFDIR))
    goto out_inv_iroot;
  if (!(iroot->i_nlink))
    goto out_inv_iroot;

  //Create dentry and link in superblock
  if (!(sb->s_root = d_alloc_root(iroot)))
    goto out_no_dentry;
  printk(KERN_DEBUG "...: root linked\n");

  return (0);

 out_bad_blocksize:
  if (!silent)
    printk("SFS-fs: Invalid block size for device\n");
  goto out;

 out_no_sb:
  if(!silent)
    printk("SFS-fs: Can't read super block from device\n");
  goto out;

 out_bad_magic:
  if(!silent)
    printk("SFS-fs: Bad magic number on defice %s\n", sb->s_id);
  goto out_brelease;

 out_no_map:
  if(!silent)
    printk("SFS-fs: Can't create maps\n");
  ret = -ENOMEM;
  goto out_brelease;

 out_no_iroot:
  if(!silent)
    printk("SFS-fs: Can't find root inode\n");
  ret = PTR_ERR(iroot);
  goto out_free_map;

 out_inv_iroot:
  if(!silent)
    printk("SFS-fs: Corrupted iroot's data\n");
  ret = PTR_ERR(iroot);
  goto out_free_map;

 out_no_dentry:
  if(!silent)
    printk("SFS-fs: Can't alloc dentry\n");
  iput(iroot);
  ret = -ENOMEM;
  goto out_free_map;

 out_err_map:
  if(!silent)
    printk("SFS-fs: Can't read blocks maps\n");
 out_free_map:
  block = sbi->s_imap_blocks + sbi->s_imap_blocks;
  for(i = 0; i < block && map[i]; i++)
    brelse(map[i]);
  kfree(map);

 out_brelease:
  brelse(bh);

 out:
  STORE_SBI(sb, NULL);
  kfree(sbi);
  return (ret);
}

//Return superblock
int
sfs_get_sb(struct file_system_type *fs_type,
	      int flags,
	      const char *dev_name,
	      void *data,
	      struct vfsmount *mnt)
{
  printk("SFS: get_sb\n");

  //Create a superblock
  return get_sb_bdev(fs_type,
		     flags,
		     dev_name,
		     data,
		     &sfs_fill_super,
		     mnt);
}

//Kill superblock
void
sfs_kill_sb(struct super_block *sb)
{
  printk("SFS: kill_sb\n");

  //Kill superblock
  kill_block_super(sb);
}

/*
** ********************
** * MODULE FUNCTIONS *
** ********************
*/

void	sfs_init_once(void *ptr)
{
  struct sfs_inode_info	*inode = ptr;

  inode_init_once(&inode->vfs_inode);
}

//Module Entry point
int	sfs_init_module(void)
{
  int	err;

  printk("SFS-fs: init_module\n");

  //Allocate inode cache
  sfs_inode_cache = kmem_cache_create("sfs_inode_cache",
				      sizeof(struct sfs_inode_info),
				      0,
				      0,
				      sfs_init_once);
  if (!sfs_inode_cache)
    return -ENOMEM;

  //Register filesystem
  err = register_filesystem(&sfs_fs_type);
  return (err);
}

//Module Exit
static void	sfs_cleanup_module(void)
{
  printk("SFS-fs: cleanup_module\n");

  //Free inode cache
  kmem_cache_destroy(sfs_inode_cache);

  //Unregister FS
  unregister_filesystem(&sfs_fs_type);
}

module_init(sfs_init_module);
module_exit(sfs_cleanup_module);

MODULE_LICENSE("GPL");
