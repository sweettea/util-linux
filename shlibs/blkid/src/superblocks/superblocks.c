/*
 * superblocks.c - reads information from filesystem and raid superblocks
 *
 * Copyright (C) 2008-2009 Karel Zak <kzak@redhat.com>
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>

#include "blkdev.h"
#include "blkidP.h"

#include "superblocks.h"

/**
 * SECTION:superblocks
 * @title: Superblocks probing
 * @short_description: filesystems and raids superblocks probing.
 *
 * The library API has been originaly designed for superblocks probing only.
 * This is reason why some *deprecated* superblock specific functions don't use
 * '_superblocks_' namespace in the function name. Please, don't use these
 * functions in new code.
 *
 * The 'superblocks' probers support NAME=value (tags) interface only. The
 * superblocks probing is enabled by default (and controled by
 * blkid_probe_enable_superblocks()).
 *
 * Currently supported tags:
 *
 * @TYPE: filesystem type
 *
 * @SEC_TYPE: secondary filesystem type
 *
 * @LABEL: filesystem label
 *
 * @LABEL_RAW: raw label from FS superblock
 *
 * @UUID: filesystem UUID (lower case)
 *
 * @UUID_RAW: raw UUID from FS superblock
 *
 * @EXT_JOURNAL: external journal UUID
 *
 * @USAGE:  usage string: "raid", "filesystem", ...
 *
 * @VERSION: filesystem version
 *
 * @MOUNT: cluster mount name (?) -- ocfs only
 *
 * @SBMAGIC: super block magic string [not-implemented yet]
 *
 * @SBOFFSET: offset of superblock [not-implemented yet]
 *
 * @FSSIZE: size of filessystem [not-implemented yet]
 */

static int superblocks_probe(blkid_probe pr, struct blkid_chain *chn);
static int superblocks_safeprobe(blkid_probe pr, struct blkid_chain *chn);

static int blkid_probe_set_usage(blkid_probe pr, int usage);


/*
 * Superblocks chains probing functions
 */
static const struct blkid_idinfo *idinfos[] =
{
	/* RAIDs */
	&linuxraid_idinfo,
	&ddfraid_idinfo,
	&iswraid_idinfo,
	&lsiraid_idinfo,
	&viaraid_idinfo,
	&silraid_idinfo,
	&nvraid_idinfo,
	&pdcraid_idinfo,
	&highpoint45x_idinfo,
	&highpoint37x_idinfo,
	&adraid_idinfo,
	&jmraid_idinfo,
	&lvm2_idinfo,
	&lvm1_idinfo,
	&snapcow_idinfo,
	&luks_idinfo,

	/* Filesystems */
	&vfat_idinfo,
	&swsuspend_idinfo,
	&swap_idinfo,
	&xfs_idinfo,
	&ext4dev_idinfo,
	&ext4_idinfo,
	&ext3_idinfo,
	&ext2_idinfo,
	&jbd_idinfo,
	&reiser_idinfo,
	&reiser4_idinfo,
	&jfs_idinfo,
	&udf_idinfo,
	&iso9660_idinfo,
	&zfs_idinfo,
	&hfsplus_idinfo,
	&hfs_idinfo,
	&ufs_idinfo,
	&hpfs_idinfo,
	&sysv_idinfo,
        &xenix_idinfo,
	&ntfs_idinfo,
	&cramfs_idinfo,
	&romfs_idinfo,
	&minix_idinfo,
	&gfs_idinfo,
	&gfs2_idinfo,
	&ocfs_idinfo,
	&ocfs2_idinfo,
	&oracleasm_idinfo,
	&vxfs_idinfo,
	&squashfs_idinfo,
	&netware_idinfo,
	&btrfs_idinfo
};

/*
 * Driver definition
 */
const struct blkid_chaindrv superblocks_drv = {
	.id           = BLKID_CHAIN_SUBLKS,
	.name         = "superblocks",
	.dflt_enabled = TRUE,
	.dflt_flags   = BLKID_SUBLKS_DEFAULT,
	.idinfos      = idinfos,
	.nidinfos     = ARRAY_SIZE(idinfos),
	.has_fltr     = TRUE,
	.probe        = superblocks_probe,
	.safeprobe    = superblocks_safeprobe
};


/**
 * blkid_known_fstype:
 * @fstype: filesystem name
 *
 * Returns: 1 for known filesytems, or 0 for unknown filesystem.
 */
int blkid_known_fstype(const char *fstype)
{
	int i;

	if (!fstype)
		return 0;

	for (i = 0; i < ARRAY_SIZE(idinfos); i++) {
		const struct blkid_idinfo *id = idinfos[i];
		if (strcmp(id->name, fstype) == 0)
			return 1;
	}
	return 0;
}

/*
 * The blkid_do_probe() backend.
 */
static int superblocks_probe(blkid_probe pr, struct blkid_chain *chn)
{
	int i = 0;

	if (!pr || chn->idx < -1)
		return -1;
	if (chn->idx < -1)
		return -1;
	blkid_probe_chain_reset_vals(pr, chn);

	DBG(DEBUG_LOWPROBE,
		printf("--> starting probing loop [SUBLKS idx=%d]\n",
		chn->idx));

	i = chn->idx + 1;

	for ( ; i < ARRAY_SIZE(idinfos); i++) {
		const struct blkid_idinfo *id;
		const struct blkid_idmag *mag;
		int hasmag = 0;

		chn->idx = i;

		if (chn->fltr && blkid_bmp_get_item(chn->fltr, i))
			continue;

		id = idinfos[i];
		mag = id->magics ? &id->magics[0] : NULL;

		/* try to detect by magic string */
		while(mag && mag->magic) {
			blkid_loff_t off;
			unsigned char *buf;

			off = mag->kboff + ((blkid_loff_t) mag->sboff >> 10);
			buf = blkid_probe_get_buffer(pr, off << 10, 1024);

			if (buf && !memcmp(mag->magic,
					buf + (mag->sboff & 0x3ff), mag->len)) {
				DBG(DEBUG_LOWPROBE, printf(
					"%s: magic sboff=%u, kboff=%ld\n",
					id->name, mag->sboff, mag->kboff));
				hasmag = 1;
				break;
			}
			mag++;
		}

		if (hasmag == 0 && id->magics && id->magics[0].magic)
			/* magic string(s) defined, but not found */
			continue;

		/* final check by probing function */
		if (id->probefunc) {
			DBG(DEBUG_LOWPROBE, printf(
				"%s: call probefunc()\n", id->name));
			if (id->probefunc(pr, mag) != 0)
				continue;
		}

		/* all cheks passed */
		if (chn->flags & BLKID_SUBLKS_TYPE)
			blkid_probe_set_value(pr, "TYPE",
				(unsigned char *) id->name,
				strlen(id->name) + 1);
		if (chn->flags & BLKID_SUBLKS_USAGE)
			blkid_probe_set_usage(pr, id->usage);

		DBG(DEBUG_LOWPROBE,
			printf("<-- leaving probing loop (type=%s) [SUBLKS idx=%d]\n",
			id->name, chn->idx));
		return 0;
	}
	DBG(DEBUG_LOWPROBE,
		printf("<-- leaving probing loop (failed) [SUBLKS idx=%d]\n",
		chn->idx));
	return 1;
}

/*
 * This is the same function as blkid_do_probe(), but returns only one result
 * (cannot be used in while()) and checks for ambivalen results (more
 * filesystems on the device) -- in such case returns -2.
 *
 * The function does not check for filesystems when a RAID signature is
 * detected.  The function also does not check for collision between RAIDs. The
 * first detected RAID is returned.
 */
static int superblocks_safeprobe(blkid_probe pr, struct blkid_chain *chn)
{
	struct blkid_prval vals[BLKID_NVALS_SUBLKS];
	int nvals = BLKID_NVALS_SUBLKS;
	int idx = -1;
	int count = 0;
	int intol = 0;
	int rc;

	while ((rc = superblocks_probe(pr, chn)) == 0) {
		if (!count) {
			/* save the first result */
			nvals = blkid_probe_chain_copy_vals(pr, chn, vals, nvals);
			idx = chn->idx;
		}
		count++;

		if (idinfos[chn->idx]->usage & BLKID_USAGE_RAID)
			break;
		if (!(idinfos[chn->idx]->flags & BLKID_IDINFO_TOLERANT))
			intol++;
	}
	if (rc < 0)
		return rc;		/* error */
	if (count > 1 && intol) {
		DBG(DEBUG_LOWPROBE,
			printf("ERROR: superblocks chain: "
			       "ambivalent result detected (%d filesystems)!\n",
			       count));
		return -2;		/* error, ambivalent result (more FS) */
	}
	if (!count)
		return 1;		/* nothing detected */

	/* restore the first result */
	blkid_probe_chain_reset_vals(pr, chn);
	blkid_probe_append_vals(pr, vals, nvals);
	chn->idx = idx;

	return 0;
}

static int blkid_probe_set_usage(blkid_probe pr, int usage)
{
	char *u = NULL;

	if (usage & BLKID_USAGE_FILESYSTEM)
		u = "filesystem";
	else if (usage & BLKID_USAGE_RAID)
		u = "raid";
	else if (usage & BLKID_USAGE_CRYPTO)
		u = "crypto";
	else if (usage & BLKID_USAGE_OTHER)
		u = "other";
	else
		u = "unknown";

	return blkid_probe_set_value(pr, "USAGE", (unsigned char *) u, strlen(u) + 1);
}
