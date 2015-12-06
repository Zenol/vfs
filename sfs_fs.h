#ifndef SFS_FS_H_
# define SFS_FS_H_

//sfs_super_block->s_state :
# define	SFS_VALID_FS	1
# define	SFS_ERROR_FS	2
# define	SFS_MOUNTED	4

//////////////////
//SFS constants //
//////////////////
//SFS Magic
# define	SFS_MAGIC		0x3234
//Block size
# define	SFS_BLOCK_SIZE		4096
//Log2 of block size
# define	SFS_BLOCK_LOG_SIZE	12 // 2^12 = 4096
//Number of bit in a block
# define	BIT_PER_BLOCK		(SFS_BLOCK_SIZE << 3) // BlockSize * 8
//Number of indirect elements
# define	INDIRECT_BY_BLOCK	(SFS_BLOCK_SIZE / sizeof(sfs_block_idx))
//Number of double indirect pointing to indirect blocks
# define	DBINDIRECT_BY_BLOCK	(SFS_BLOCK_SIZE / sizeof(__u32))
//Maximum link to an inode
# define	SFS_MAX_LINK		65530
//How much inode can be stored in one block
# define	INODE_PER_BLOCK		\
  (SFS_BLOCK_SIZE / sizeof(struct sfs_inode))
//Inode data's field
# define	INO_DATA_COUNT		10
//Superblock's inode ID
# define	SFS_ROOT_INO		2

struct	sfs_block_idx
{
  __u32	b_start;
  __u32	b_count;
};

struct	sfs_super_block
{
  __u32	s_nblocks;
  __u32	s_ninodes;
  __u32	s_inode_blocks;
  __u32	s_imap_blocks;
  __u32	s_bmap_blocks;
  __u32	s_firstdatablock;
  __u16	s_state;
  __u16	s_namelen;
  __u16 s_magic;
  __u16 s_unused;
};

struct	sfs_inode
{
  __u16	i_mode;
  __u16	i_nlink;
  __u16	i_uid;
  __u16	i_gid;
  __u32	i_size;
  __u32	i_atime;
  __u32	i_mtime;
  __u32	i_ctime;
  /*
  ** 3 (pos/count)
  ** 1 indirect block (pos -> pos/count)
  ** 1 indirect indirect block (pos -> pos -> pos/count)
  */
  __u32	i_data[INO_DATA_COUNT];
};

struct	sfs_dirent
{
  __u32	d_ino;
  char	d_name[0];
};

#endif /* !SFS_FS_H_ */
