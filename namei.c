/*
 * sfs/namei.c for SFS
 *
 * Copyright (C) 2009, Jeremy Cochoy
 */
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include "sfs_fs.h"
#include "sfs.h"

/**
 * sfs_dentry_by_name - Search a directory entry in a directory
 *
 * The page is returned mapped and locked into @res_page,
 * and the dentry found (if not ERR_PTR) is guarenteed to be valid.
 *
 * @dir Directory's inode where are located sfs_dirent
 * @child The child we are looking for
 * @res_page Where to save page_ptr found
 *
 * Returns %NULL or %PTR_ERR code
 */
struct sfs_dirent*
sfs_dentry_by_name(struct inode *dir, struct qstr *child, struct page **res_page)
{
  //How much pages in this file?
  unsigned long		cnt_pages	= sfs_count_pages(dir);
  //Page index
  unsigned long		pidx		= 0;
  //SFS directory entry
  struct sfs_dirent*	dent;
  //Page address
  char			*kaddr;

  printk(KERN_DEBUG " sfs_lookup\n");

  //Page we are looking for
  *res_page		= NULL;

  //On each page
  for(pidx = 0;pidx < cnt_pages; pidx++)
    {
      printk(KERN_DEBUG "   page %lu\n", pidx);
      //Get page or next page
      *res_page = sfs_get_page(dir, pidx);
      if (IS_ERR(*res_page))
	continue;

      //Lock the page
      lock_page(*res_page);

      //End is ino = 0 (An entry can't be broken into 2 pages!)
      kaddr = (void*)page_address(*res_page);
      dent = (void*)kaddr;
      while (dent->d_ino)
	{
	  if (!(strcmp(dent->d_name, child->name)))
	    goto entry_found;
	  dent = sfs_next_dentry(dent, strlen(dent->d_name));
	}
      unlock_page(*res_page);
      sfs_put_page(*res_page);
    }

  goto no_entry;

 entry_found:
  return dent;

 no_entry:
  *res_page = 0;
  return NULL;
}

/**
 * sfs_lookup - Search a inode by it's name in dir directory
 * @dir Directory's inode where are located sfs_dirent
 * @entry dentry where we must store inode
 *
 * Returns %NULL or %PTR_ERR code
 */
struct dentry *
sfs_lookup(struct inode *dir, struct dentry *entry, struct nameidata *nameidata)
{
  //Page used
  struct page		*page		= NULL;
  //SFS directory entry
  struct sfs_dirent*	dent;
  //Inode we are looking for
  struct inode*		inode;

  printk(KERN_DEBUG " sfs_lookup\n");

  dent = sfs_dentry_by_name(dir, &entry->d_name, &page);
  if (!dent)
    goto no_entry;

  //entry found
  inode = sfs_iget(dir->i_sb, dent->d_ino);
  unlock_page(page);
  sfs_put_page(page);
  if (IS_ERR(inode))
    return (void*)inode;
  d_add(entry, inode);
  printk(KERN_DEBUG "   ->found\n");
  return NULL;

 no_entry:
  d_add(entry, NULL);
  printk(KERN_DEBUG "   ->no\n");
  return NULL;
}

int sfs_mknod
(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
{
  struct inode *inode;
  int		err = -EINVAL;

  printk(KERN_DEBUG "sfs_mknod\n");

  //Create a new inode
  inode = sfs_new_inode(dir->i_sb);
  if (IS_ERR(inode))
    return PTR_ERR(inode);
  if (inode)
    {
      //Set up data and ops
      inode->i_mode = mode;
      sfs_set_inode_ops(inode, rdev);
      //Store into directory
      err = sfs_add_link(dentry, inode);
      if(err)
	goto err;
      if (!(mode & S_IFDIR))
	inode_dec_link_count(inode);
      mark_inode_dirty(inode);
      //Link inode into dirent
      d_instantiate(dentry, inode);
      return 0;
    }

 err:
  //Free inode bit
  sfs_put_binode(inode->i_sb, inode->i_ino);
  //Free inode
  inode_dec_link_count(inode);
  iput(inode);
  return err;
}

int sfs_link
(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
  struct inode *inode = old_dentry->d_inode;
  int			err = -EMLINK;

  printk(KERN_DEBUG "sfs_link\n");

  if(inode->i_nlink >= SFS_MAX_LINK)
    goto err;

  err = sfs_add_link(dentry, inode);
  if(!err)
    {
      //Inode change time = dir change time
      inode->i_ctime = dir->i_ctime;
      //Increment inode use
      atomic_inc(&inode->i_count);
      //Mark dirty
      mark_inode_dirty(inode);
      //Link inode into dirent
      d_instantiate(dentry, inode);
      return 0;
    }
  //inode->i_nlink--
  inode_dec_link_count(inode);

 err:
  iput(inode);
  return err;
}

int sfs_unlink
(struct inode *dir, struct dentry *dentry)
{
  struct inode		 *inode = dentry->d_inode;
  int			err = -ENOENT;
  struct sfs_dirent	*dent;
  struct page		*page;

  printk(KERN_DEBUG "sfs_unlink\n");

  //Search the dentry
  dent = sfs_dentry_by_name(dir, &dentry->d_name, &page);
  if(!dent)
    {
      printk(KERN_DEBUG "   ->nodent\n");

      goto err;
    }

  err = sfs_delete_entry(dent, page);
  if(err)
    {
      printk(KERN_DEBUG "   ->errdel\n");

      goto err;
   }


  inode_dec_link_count(inode);
  inode->i_ctime = dir->i_ctime;
  err = 0;

 err:
  if(err != 0)
    printk(" Oula, unlink err:%d\n", (int)err);
  return err;
}

static int
sfs_mkdir
(struct inode *dir, struct dentry *dentry, int mode)
{
  int	err;
  err = sfs_mknod(dir, dentry, mode | S_IFDIR, 0);
  if (err < 0)
    return err;
  inode_inc_link_count(dir);
  return err;
}

static int
sfs_rmdir(struct inode *dir, struct dentry *dentry)
{
  struct inode	*inode = dentry->d_inode;
  int		err = -ENOTEMPTY;

  if(sfs_empty_dir(inode))
    {
      err = sfs_unlink(dir, dentry);
      //Remove parent and self link
      if(!err)
	{
	  inode_dec_link_count(dir);
	  inode_dec_link_count(inode);
	}
    }

  return err;
}

static int
sfs_create
(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
  return sfs_mknod(dir, dentry, mode, 0);
}

static int
sfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
  int	err = -ENAMETOOLONG;
  int	len = strlen(symname) + 1;
  struct inode	*inode;

  if(len > SFS_BLOCK_SIZE)
    goto out;

  inode = sfs_new_inode(dir->i_sb);

  if (IS_ERR(inode))
    return PTR_ERR(inode);

  if (!inode)
    goto out;

  printk( "symlink : lim %d\n", (int)dir->i_sb->s_blocksize);
  printk(" going symlink %s\n", symname);

  //Set up data and ops
  inode->i_mode = S_IFLNK | 0777;
  sfs_set_inode_ops(inode, 0);
  //Store symlink
  err = page_symlink(inode, symname, len);
  if (err)
    {
      printk(" page_symlink_err !!!\n");
    goto out_fail;
    }

  err = sfs_add_link(dentry, inode);
  if(err)
    goto out_fail;

  //inode->i_nlink--
  inode_dec_link_count(inode);
  //Mark dirty
  mark_inode_dirty(inode);
  //Link inode into dirent
  d_instantiate(dentry, inode);
  return 0;

 out:
  return err;

 out_fail:
  inode_dec_link_count(inode);
  iput(inode);
  goto out;
}

struct inode_operations sfs_dir_iops =
  {
    .lookup	= sfs_lookup,
    .create	= sfs_create,
    .mknod	= sfs_mknod,
    .mkdir	= sfs_mkdir,
    .rmdir	= sfs_rmdir,
    .link	= sfs_link,
    .unlink	= sfs_unlink,
    .symlink	= sfs_symlink,
  };
