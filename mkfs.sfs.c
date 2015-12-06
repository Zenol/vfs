#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <mntent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <time.h>
#include "sfs_fs.h"

/////////
//DEFINES
/////////
#define	EXIT_USAGE	4
#define	EXIT_DIE	16
#define	EXIT_DONE	0
#define	IROOT_DEF_MODE	(S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

/////////
//GLOBALS
/////////
//program name
char	*mkfs_name = 0;
//device path resolving buffer
char	path_buff[PATH_MAX + 1];
//device name
char	*device = 0;
//inode number
__u32	count_inodes = 0;
//blocks number
__u32	count_blocks = 0;
//blocks used by inodes
__u32	count_iblocks = 0;
//blocks used by imap
__u32	count_imap = 0;
//blocks used by bmap
__u32	count_bmap = 0;
//first data block location
__u32 firstdatablock = 0;
//device size
__u32	device_size = 0;
//block device? (or regular file)
char	device_isblk = 0;
//device file descriptor
int	device_fd = -1;
//file name len limit
__u32	max_namelen = 0;

///////
//TOOLS
///////
//Usage message
void	usage(void)
{
  printf("%s [-nXX] [-iXX] /dev/name [blocks]\n", mkfs_name);
  exit(EXIT_USAGE);
}

//Output warnings
void	warn(const char *msg)
{
  printf("%s: Warning: %s\n", mkfs_name, msg);
}

//Output errors
void	die(const char *msg)
{
  printf("%s: %s\n", mkfs_name, msg);
  exit(EXIT_DIE);
}

////
//Check device
////
void	check_device(void)
{
  FILE	*f;
  struct mntent	*ent;
  struct stat st;
  char	c;

  //Test BLOCK
  if (stat(device, &st) == -1)
    die("Can't stat device");
  if (!S_ISBLK(st.st_mode))
    {
      printf("%s: %s is not a block special device.\n", mkfs_name, device);
      c = 0;
      while (c != 'y' && c!= 'Y' && c != 'n' && c != 'N')
	{
	  printf("Proceed anyway? (y,n) ");
	  scanf("%c", &c);
	}
      if (c == 'n' || c == 'N')
	exit(EXIT_DONE);
      device_size = st.st_size / 512;
    }
  else
    device_isblk = 1;

  //Test MOUNTED
  if (!(f = setmntent(MOUNTED, "r")))
    {
      warn("Can't check mounted fs");
      return;
    }
  while ((ent = getmntent(f)))
    {
      if (!strcmp(ent->mnt_fsname, device))
      	die("File system already mounted!");
    }
  endmntent(f);
}

////
//Check BLOCKS
////
void	check_blocks(void)
{
  off_t lseek_pos;

  if (!device_size && !device_isblk)
    die ("Empty file!");

  //Open device
  if(device_isblk)
    device_fd = open(device, O_RDWR | O_EXCL);
  else
    device_fd = open(device, O_RDWR);
  if (device_fd == -1)
    die("Can't open device");

  //Get block size
  if (device_isblk)
    {
      //by IOCTL
      if (ioctl(device_fd, BLKGETSIZE, &device_size) == -1)
	{
	  //or LSEEK
	  lseek_pos = lseek(device_fd, 0, SEEK_END) / 512;
	  if (lseek_pos != -1)
	    lseek(device_fd, 0, SEEK_END);
	  if (lseek_pos == -1 || !lseek_pos)
	    {
	      close(device_fd);
	      die ("Can't get block size");
	    }
	  else
	    device_size = (__u32)lseek_pos;
	}
    }

  //Convert device_size in blocks
  //(device_size * 512 / 4096 = device_size / 8)
  device_size = device_size / 8;

  //Count how much block used and store in count_blocks
  if (!device_size)
    die("Device size too small");
  if (count_blocks > device_size)
    {
      printf("You asked %d blocks but there is only %d!\n", count_blocks, device_size);
      die("Not enougth space");
    }
  if (!count_blocks)
    count_blocks = device_size;
  printf("SFS will use %d blocks (4096bytes each)\n", count_blocks);
}

void	check_inodes_and_maps(void)
{
  //Use 1% in inodes
  if(!count_inodes)
    {
      count_iblocks = (count_blocks / 100);
      if (!count_iblocks)
	count_iblocks++;
      count_inodes = INODE_PER_BLOCK * count_iblocks;
    }
  //Count how blocks needed
  else
    {
      count_iblocks = count_inodes / INODE_PER_BLOCK;
      if (count_inodes % INODE_PER_BLOCK)
	count_iblocks++;
    }

  //Count blocks used by inode map
  count_imap = count_iblocks / BIT_PER_BLOCK;
  if (count_iblocks % BIT_PER_BLOCK)
    count_imap++;

  //Count blocks used by block map
  count_bmap = count_blocks / BIT_PER_BLOCK;
  if (count_blocks % BIT_PER_BLOCK)
    count_bmap++;

  printf("%d inodes used in %d blocks\n", count_inodes, count_iblocks);
  printf("%d blocks used by inode map\n", count_imap);
  printf("%d blocks used by block map\n", count_bmap);

  firstdatablock = 1 + count_imap + count_bmap + count_iblocks; //+1 -> SuperBlock
  if (firstdatablock >= count_blocks)
    die("Not enought block to store the whole filesystem!");
  printf("%d blocks reserved by filesystem\n", firstdatablock);
}

void	write_sb(void)
{
  __u8	block[SFS_BLOCK_SIZE];
  struct sfs_super_block *sb = (void*)block;

  //Set superblock data
  memset(block, 0, sizeof(block));
  sb->s_nblocks = count_blocks;
  sb->s_ninodes = count_inodes;
  sb->s_inode_blocks = count_iblocks;
  sb->s_imap_blocks = count_imap;
  sb->s_bmap_blocks = count_bmap;
  sb->s_firstdatablock = firstdatablock;
  sb->s_state = SFS_VALID_FS;
  sb->s_namelen = max_namelen;
  sb->s_magic = SFS_MAGIC;

  //Write on disk
  printf("Writing superblock...\r");
  if (write(device_fd, block, sizeof(block)) == -1)
    die ("Can't write superblock");
}

void	write_imap(void)
{
  __u8	*imap;

  imap = calloc(count_imap, SFS_BLOCK_SIZE);
  //Inode 0 -> Can't be use, historycal purpose
  //Inode 1 -> Linked to bad block (historical purpose too)
  //Inode 3 -> Inode by SB block
  *imap |= 7; // 111b -> 3 first inodes used
  printf("Writing imap...\r");

  //Write on disk
  if (write(device_fd, imap, count_imap << SFS_BLOCK_LOG_SIZE) == -1)
    die ("Can't write imap");

  free(imap);
}

void	write_bmap(void)
{
  __u8	*bmap;
  int	i;

  bmap = calloc(count_bmap, SFS_BLOCK_SIZE);
  printf("Writing bmap...\r");

  //All block, from start to firstdatablock are used
  for(i = 0; i < firstdatablock; i++)
    bmap[i / 8] |= (1 << (i % 8));

  //Write on disk
  if (write(device_fd, bmap, count_bmap << SFS_BLOCK_LOG_SIZE) == -1)
    die ("Can't write bmap");

  free(bmap);
}

void		write_ino_table(void)
{
  __u8			*itab;
  struct sfs_inode	*iroot;

  itab = calloc(count_iblocks, SFS_BLOCK_SIZE);
  printf("Writing inode table...\r");

  //Store the root inode (/)
  iroot = (void*)itab;
  //Inode 0 and 1 reserved
  iroot += 2;
  //Set mode
  iroot->i_mode = IROOT_DEF_MODE;
  iroot->i_uid = 0;
  iroot->i_gid = 0;
  iroot->i_size = 0;
  iroot->i_atime = time(0);
  iroot->i_mtime = iroot->i_atime;
  iroot->i_ctime = iroot->i_atime;
  iroot->i_nlink = 2;
  bzero(iroot->i_data, sizeof(iroot->i_data));

  //Writing on disk
  if (write(device_fd, itab, count_iblocks << SFS_BLOCK_LOG_SIZE) == -1)
    die ("Can't write inode table");
}

//MKFS.SFS ENTRY POINT
int	main(int ac, char *av[])
{
  char	c;
  char	*err;

  //Remember program name
  mkfs_name = av[0];

  //Check opts
  opterr = 0;
  while((c = getopt(ac, av, "i:n:")) != -1)
    {
      switch(c)
	{
	case 'i':
	  count_inodes = strtoul(optarg, &err, 0);
	  if (*err)
	    die("Invalid inode number");
	  break;
	case 'n':
	  max_namelen = strtoul(optarg, &err, 0);
	  if (*err)
	    die("Invalid file name limit");
	  break;
	default :
	  usage();
	}
    }
  ac -= optind;
  av += optind;

  if (ac < 1)
    usage();

  //Convert RELative path to ABSolute
  if (realpath(av[0], path_buff))
    {
      path_buff[PATH_MAX] = 0;
      device = path_buff;
    }
  else
    device = av[0];
  if (ac == 2)
    {
      count_blocks = strtoul(av[1], &err, 0);
      if (*err)
	die("Invalid block number");
    }

  //Write FS:
  check_device();
  check_blocks();
  check_inodes_and_maps();
  write_sb();
  write_imap();
  write_bmap();
  write_ino_table();

  return EXIT_DONE;
}
