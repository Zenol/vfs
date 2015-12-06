/*
 * sfs/sfs.h for SFS
 *
 * Copyright (C) 2009, Jeremy Cochoy
 */
#ifndef SFS_H_
# define SFS_H_

# define	SBI(sb)		struct sfs_sb_info *sbi = (sb)->s_fs_info
# define	STORE_SBI(sb, sbi)	(sb)->s_fs_info = (void*)(sbi)
# define	SBI_PTR(sb)	((struct sfs_sb_info*)((sb)->s_fs_info))

# define	DEPH_NOTFOUND	0
# define	DEPH_DIRECT	1
# define	DEPH_UNDIRECT	2
# define	DEPH_DBUNDIRECT	3

struct	sfs_sb_info	{
  //SFS data
  u32	s_nblocks;
  u32	s_ninodes;
  u32	s_firstinodeblock;
  u32	s_inode_blocks;
  u32	s_imap_blocks;
  u32	s_bmap_blocks;
  u32	s_firstdatablock;
  u16	s_state;
  u16	s_namelen;
  //Driver data
  struct buffer_head	*s_bh;
  struct buffer_head	**s_imap;
  struct buffer_head	**s_bmap;
};

struct		sfs_inode_info	{
  u32		i_data[INO_DATA_COUNT];
  struct inode	vfs_inode;
};

////////////////////////
// SHARED GLOBAL DATA //
////////////////////////
extern struct address_space_operations sfs_address_space_ops;
extern struct file_operations	sfs_file_ops;
extern struct inode_operations	sfs_file_iops;
extern struct file_operations	sfs_dir_ops;
extern struct inode_operations	sfs_dir_iops;
extern struct inode_operations sfs_symlink_iops;

///
/// SB
///
//Get superblock
extern int
sfs_get_sb(struct file_system_type *fs_type,
	      int flags,
	      const char *devname,
	      void *data,
	      struct vfsmount *vfsm);

//Kill superblock
extern void
sfs_kill_sb(struct super_block *sb);
//Get an inode by ID
struct inode
*sfs_iget(struct super_block *sb, ino_t ino);

///
/// INODES
///
//Read inode from disk
struct sfs_inode*
sfs_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh);
//Create a new inode
struct inode*
sfs_new_inode(struct super_block *sb);
//Set inode OPS
void
sfs_set_inode_ops(struct inode *inode, dev_t rdev);
//Truncate an inode
void
sfs_truncate(struct inode *inode);

///
/// PAGES
///
//Get page
struct page*
sfs_get_page(struct inode *ino, unsigned long index);
//Put page
void
sfs_put_page(struct page* page);
//Get a block
int sfs_get_block
(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create);
//Prepare write
int __sfs_write_begin
(struct file *file, struct address_space *mapping,
 loff_t pos, unsigned len, unsigned flags,
 struct page **pagep, void **fsdata);

///
/// BMAPS
///
//Get a ino (mark bit locked)
int
sfs_get_binode(struct super_block *sb);
//Get a ino (mark bit unlocked)
int
sfs_put_binode(struct super_block *sb, unsigned long ino);
//Get a block (mark bit locked)
int
sfs_get_bblock(struct super_block *sb);
//Get a block after start (mark bit locked)
int
sfs_get_bblock_after(struct super_block *sb, unsigned long start);
//Get a block (mark bit unlocked)
int
sfs_put_bblock(struct super_block *sb, unsigned long ino);

///
/// DIR
///
//Link a file into a directory (inc file link_count)
int
sfs_add_link(struct dentry *dentry, struct inode *inode);
//Delete a sfs_dentry located into page
int
sfs_delete_entry(struct sfs_dirent *dent, struct page *page);
//Check if a directory is empty
int
sfs_empty_dir(struct inode *inode);

///
/// FILE
///
int
sfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat);

///
/// ITREE
///
//Find a block
int
sfs_find_block(struct inode *inode, sector_t *iblock,
		       unsigned short *deph, unsigned int *blk);
//Alocate a block for inode
int
sfs_alloc_block(struct inode *inode,  sector_t *iblock,
		unsigned short *deph, unsigned int *blk);

/*
** ********
** * MISC *
** ********
*/
extern inline struct sfs_inode_info	*sfs_i(struct inode* inode)
{
  return container_of(inode, struct sfs_inode_info, vfs_inode);
}
extern inline struct sfs_super_block	*sfs_sb(struct super_block *sb)
{
  SBI(sb);

  return (void*)sbi->s_bh->b_data;
}

/**
 * sfs_next_dentry - Goto to the next dir-entry in the current page
 * @dent sfs direntry
 * @len strlen(dent->d_name)
 *
 * Returns new ptr (can be out of memory!)
 */
extern inline struct sfs_dirent*
sfs_next_dentry(struct sfs_dirent* dent, int len)
{
  return ((void*)((char*)dent + len + 1 + sizeof(dent->d_ino)));
}

//Count pages
extern inline int
sfs_count_pages(struct inode *inode)
{
  //Size = 0 -> 0 pages
  //Size = 1 -> 1 page
  //Well, we add PAGE_CACHE_SIZE - 1 and shift
  return (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}

//Count blocks

//Count pages
extern inline loff_t
sfs_count_blocks(struct inode *inode)
{
  //Size = 0 -> 0 pages
  //Size = 1 -> 1 page
  //Well, we add PAGE_CACHE_SIZE - 1 and shift
  return ((loff_t)inode->i_size + 512 - 1) >> 9;
}

#endif /* !SFS_H_ */
