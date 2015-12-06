/*
 * sfs/dir.c for SFS
 *
 * Copyright (C) 2009, Jeremy Cochoy
 */
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include "sfs_fs.h"
#include "sfs.h"

//how add to all f_pos (. and ..)
#define	IDX_ADD		2

static int	sfs_readdir
(struct file *file, void *dirent, filldir_t filldir)
{
  //Get page index
  unsigned long	pidx		= file->f_pos >> PAGE_CACHE_SHIFT;
  //Get data offset
  unsigned long	offset		= file->f_pos & ~PAGE_CACHE_MASK;
  //File's inode
  struct inode*	inode		= file->f_dentry->d_inode;
  //Parent directory
  struct inode*	parent		= file->f_dentry->d_parent->d_inode;
  //How much pages in this file?
  unsigned long	cnt_pages	= sfs_count_pages(inode);
  //Page used
  struct page	*page		= NULL;
  //SFS directory entry
  struct sfs_dirent*		dent;
  //Page address
  char				*kaddr;
  //Len name
  int	len;

  printk(KERN_DEBUG " sfs_readdir\n");

  //Page don't exist!
  if (file->f_pos > inode->i_size + IDX_ADD)
    goto done;

  printk(KERN_DEBUG "f_pos = %d\n", (int)file->f_pos);

  printk(KERN_DEBUG "    dotdot: %u\n", (unsigned int)file->f_pos);

  //On each page
  for(;pidx < cnt_pages; pidx++)
    {
      //Get page or next page
      page = sfs_get_page(inode, pidx);
      if (IS_ERR(page))
  	continue;

      //Lock page
      lock_page(page);

      printk(KERN_DEBUG "  #page = %lu(lim:%lu) off=%lu\n", pidx, cnt_pages, offset);

      //End is ino = 0 (An entry can't be broken into 2 pages!)
      kaddr = (void*)page_address(page);
      dent = (void*)kaddr + offset;
      while (dent->d_ino)
  	{
  	  len = strlen(dent->d_name);
  	  offset = (char*)dent - kaddr;
  	  printk("      -> walk on ino:%u id %lu\n", dent->d_ino,
  		 (unsigned long)(((pidx << PAGE_CACHE_SHIFT) | offset)));
  	  //Fill file (unknow type, fs will know by checking inode latter)
  	  if (filldir(dirent, dent->d_name, len,
  		      file->f_pos = ((pidx << PAGE_CACHE_SHIFT) | offset),
  		      dent->d_ino, DT_UNKNOWN))
  	    {
  	      //Unlock the page
  	      unlock_page(page);
  	      //filldir error
  	      sfs_put_page(page);
  	      goto out;
  	    }
  	  dent = sfs_next_dentry(dent, len);
  	}
      printk(KERN_DEBUG "  #finwhile\n");

      //Unlock and put page
      unlock_page(page);
      sfs_put_page(page);
      offset = 0;
    }

  //. and .. are special directory
  //the inode value can be anything > 0 (So, we can't use the real inode, cause iroot->i_ino = 0)
  if (offset == 0 && filldir(dirent, ".", 1,
			     file->f_pos = ((pidx << PAGE_CACHE_SHIFT) | offset),
			     inode->i_ino, DT_DIR))
    goto out;
  offset = 1;
  filldir(dirent, "..", 2,
	  file->f_pos = ((pidx << PAGE_CACHE_SHIFT) | offset),
	  parent->i_ino, DT_DIR);

  //Finish, . .. and other writed.
 done:
  printk(KERN_DEBUG "  #done %u\n", (unsigned int)file->f_pos);
  file->f_pos = inode->i_size + IDX_ADD + 1;
  return 1;

  //Out, all dirent aren't already writed
 out:
  printk(KERN_DEBUG "  #out\n");
  return 0;
}

/**
 * sfs_readdir - Tell if directory is empty
 * @inode Directory's inode
 * return True if empty, False otherwise
 */
int
sfs_empty_dir
(struct inode *inode)
{
  //How much pages
  unsigned long		cnt_pages = sfs_count_pages(inode);
  //Page used
  struct page		*page = NULL;
  //SFS directory entry
  struct sfs_dirent*	dent;
  //Page index
  unsigned long		pidx;

  printk(KERN_DEBUG " sfs_emptydir\n");

  //On each page
  for(pidx = 0; pidx < cnt_pages; pidx++)
    {
      //Get page or next page
      page = sfs_get_page(inode, pidx);
      if (IS_ERR(page))
  	continue;

      //Lock page
      lock_page(page);

      //End is ino = 0
      dent  = (void*)page_address(page);
      if (dent->d_ino)
	goto no_empty;

      //Unlock and put page
      unlock_page(page);
      sfs_put_page(page);
    }

  //Return TRUE
  return 1;

 no_empty:
  //Free page
  unlock_page(page);
  sfs_put_page(page);
  //Return FALSE
  return 0;
}

/*
** Add a sfs_direntry into dentry->d_parent->d_inode (dir) linked to inode.
** Inc inode->n_link
*/
int
sfs_add_link(struct dentry *dentry, struct inode *inode)
{
  struct inode*		dir = dentry->d_parent->d_inode;
  SBI(dir->i_sb);
  const char*		name = dentry->d_name.name;
  const int		len = dentry->d_name.len;
  const int		dsize = len + sizeof(struct sfs_dirent);
  unsigned long		cnt_pages = sfs_count_pages(dir);
  struct sfs_dirent	*dent;
  struct page		*page;
  char			*kaddr;
  int			pidx ;
  int			lspace;
  int			pos;
  int			err = 0;
  int			wrlen;

  printk("sfs_add_link\n");


  //Check name len
  if(sbi->s_namelen && len >= sbi->s_namelen)
    goto inval;

  //Check dent size (name+ino must be smaller than block_size - sizeof(dent)!)
  if(PAGE_CACHE_SIZE - sizeof(dent) < dsize)
    goto nospc;

  //Search first free space
  pidx = 0;
  while(pidx < cnt_pages)
    {
      //Get page
      page = sfs_get_page(dir, pidx);
      if(IS_ERR(page))
	goto inv_page;
      lock_page(page);

      //Get dent
      kaddr = page_address(page);
      printk(KERN_DEBUG " %%%%pidx%lu\n", (unsigned long)pidx);

      //Goto page end
      for(dent = (void*)kaddr;
	  dent->d_ino;
	  dent = sfs_next_dentry(dent, strlen(dent->d_name)));

      //Check space available
      lspace = (char*)dent - kaddr;
      printk(KERN_DEBUG " %%%%lspace%lu\n", (unsigned long)lspace);
      if (lspace + sizeof(dent) + dsize < BLOCK_SIZE)
	goto add_dent;

      //Unlock and put page
      unlock_page(page);
      sfs_put_page(page);

      pidx++;
    }

  ////
  //Expend : add a new page
  ////
  printk(KERN_DEBUG " %%%%exp\n");
  //Add a page in file size
  i_size_write(dir, dir->i_size + PAGE_CACHE_SIZE);
  cnt_pages++;
  //Get page
  //  pidx is already at the great value
  page = sfs_get_page(dir, pidx);
  if(IS_ERR(page))
    goto inv_page;
  lock_page(page);
  //Get dent
  kaddr = page_address(page);
  dent = (void*)kaddr;
  goto add_dent;

 inv_page:
  sfs_put_binode(inode->i_sb, inode->i_ino);
  printk(KERN_DEBUG "   j'iput dans dir.c:sfs_add_link\n");
  iput(inode);
  return -EINVAL;

 add_dent:
  printk(" %%%% father:%lu child:%lu [page:%d]\n", dir->i_ino, inode->i_ino, pidx);
  //Get position
  pos = page_offset(page) + ((char*)dent - kaddr);
  //Set ino
  dent->d_ino = inode->i_ino;
  //Set string (strlen(name) + 1)
  strcpy(dent->d_name, name);
  //Add end
  dent = sfs_next_dentry(dent, len);
  dent->d_ino = 0;

  //Write full page on disk
  wrlen = dsize + sizeof(dent->d_ino);
  printk(" %%%% pos:%d wrlen:%d\n", pos, wrlen);
  err = __sfs_write_begin(NULL, page->mapping, pos, wrlen,
			  AOP_FLAG_UNINTERRUPTIBLE, &page, NULL);
  if(err)
    goto out_unlock;
  block_write_end(NULL, page->mapping, pos, wrlen,
		  wrlen, page, NULL);
  inode_inc_link_count(inode);

 out_unlock:
  //Unlock and put page
  unlock_page(page);
  sfs_put_page(page);

  //Mark inode and inc inode->i_nlink
  mark_inode_dirty(dir);
  return err;

 nospc:
  return -ENOSPC;
 inval:
  return -EINVAL;
}

/**
 * Page must be locked and mapped, en sfs_dentry must be valid.
 * The page is unlocked and unmaped
 */
int
sfs_delete_entry(struct sfs_dirent *dent, struct page *page)
{
  struct inode		*inode = page->mapping->host;
  const int		lname = strlen(dent->d_name);
  struct sfs_dirent	*next_dent = sfs_next_dentry(dent, lname);
  int			memlen = PAGE_CACHE_SIZE;
  char			*kaddr = page_address(page);
  int			pos;
  int			err = 0;

  printk(" sfs_delete_entry\n");
  //Memory to re-write
  pos = (char*)dent - kaddr;
  memlen -= sizeof(dent) + lname + pos;
  //Erase dentry and move others dentries
  memcpy(dent, next_dent, memlen);

  //Write on disk
  err = __sfs_write_begin(NULL, page->mapping,
			  pos, memlen,
			  0, &page, NULL);
  if(err)
    {
      err = -EIO;
      goto unlock;
    }

  printk(KERN_DEBUG "   --delentry pos:%d len:%d\n", pos, memlen);

  if(block_write_end(NULL, page->mapping, pos, memlen,
		     memlen, page, NULL) != memlen)
    err = -EIO;
  inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
  mark_inode_dirty(inode);

  //Unlock page and return
 unlock:
  unlock_page(page);
  sfs_put_page(page);
  return err;
}

struct file_operations	sfs_dir_ops =
  {
    .read		= generic_read_dir,
    .readdir		= sfs_readdir,
  };
