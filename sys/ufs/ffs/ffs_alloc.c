/*	$OpenBSD: ffs_alloc.c,v 1.99 2014/01/25 23:31:12 guenther Exp $	*/
/*	$NetBSD: ffs_alloc.c,v 1.11 1996/05/11 18:27:09 mycroft Exp $	*/

/*-
 * Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and Network Associates Laboratories, the Security
 * Research Division of Network Associates, Inc. under DARPA/SPAWAR
 * contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA CHATS
 * research program.
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_alloc.c	8.11 (Berkeley) 10/27/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/stdint.h>
#include <sys/time.h>
#include <sys/wapbl.h>

#include <uvm/uvm_extern.h>

#include <dev/rndvar.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/ufs_wapbl.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#define ffs_fserr(fs, uid, cp) do {				\
	log(LOG_ERR, "uid %u on %s: %s\n", (uid),		\
	    (fs)->fs_fsmnt, (cp));				\
} while (0)

daddr_t		ffs_alloccg(const struct inode *, int, daddr_t, int, int);
struct buf *	ffs_cgread(const struct fs *, struct vnode *, int);
daddr_t		ffs_alloccgblk(const struct inode *, struct buf *, daddr_t,
		    int);
daddr_t		ffs_clusteralloc(const struct inode *, int, daddr_t, int, int);
ufsino_t	ffs_dirpref(const struct inode *);
daddr_t		ffs_fragextend(const struct inode *, int, daddr_t, int, int);
daddr_t		ffs_hashalloc(struct inode *, int, daddr_t, int, int,
		    daddr_t (*)(const struct inode *, int, daddr_t, int, int));
daddr_t		ffs_nodealloccg(const struct inode *, int, daddr_t, int, int);
daddr_t		ffs_mapsearch(const struct fs *, struct cg *, daddr_t, int);

int ffs1_reallocblks(void *);
#ifdef FFS2
int ffs2_reallocblks(void *);
#endif

#ifdef DIAGNOSTIC
int      ffs_checkblk(const struct inode *, daddr_t, long);
#endif

static const struct timeval	fserr_interval = { 2, 0 };


/*
 * Allocate a block in the file system.
 *
 * The size of the requested block is given, which must be some
 * multiple of fs_fsize and <= fs_bsize.
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadratically rehash into other cylinder groups, until an
 *      available block is located.
 * If no block preference is given the following hierarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *      inode for the file.
 *   2) quadratically rehash into other cylinder groups, until an
 *      available block is located.
 */
int
ffs_alloc(struct inode *ip, daddr_t lbn, daddr_t bpref, int size, int flags,
    struct ucred *cred, daddr_t *bnp)
{
	static struct timeval fsfull_last;
	const struct fs *fs;
	daddr_t bno;
	int cg;
	int error;

	*bnp = 0;
	fs = ip->i_fs;
#ifdef DIAGNOSTIC
	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		printf("dev = 0x%x, bsize = %d, size = %d, fs = %s\n",
		    ip->i_dev, fs->fs_bsize, size, fs->fs_fsmnt);
		panic("ffs_alloc: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_alloc: missing credential");
#endif /* DIAGNOSTIC */
	if (size == fs->fs_bsize && fs->fs_cstotal.cs_nbfree == 0)
		goto nospace;
	if (cred->cr_uid != 0 && freespace(fs, fs->fs_minfree) <= 0)
		goto nospace;

	if ((error = ufs_quota_alloc_blocks(ip, btodb(size), cred)) != 0)
		return (error);

	/*
	 * Start allocation in the preferred block's cylinder group or
	 * the file's inode's cylinder group if no preferred block was
	 * specified.
	 */
	if (bpref >= fs->fs_size)
		bpref = 0;
	if (bpref == 0)
		cg = ino_to_cg(fs, ip->i_number);
	else
		cg = dtog(fs, bpref);

	/* Try allocating a block. */
	bno = ffs_hashalloc(ip, cg, bpref, size, flags, ffs_alloccg);
	if (bno > 0) {
		/* allocation successful, update inode data */
		DIP_ADD(ip, blocks, btodb(size));
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}

	/* Restore user's disk quota because allocation failed. */
	(void) ufs_quota_free_blocks(ip, btodb(size), cred);

	if (flags & B_CONTIG) {
		/*
		 * Fail silently -- it's up to our caller to report errors.
		 */
		return (ENOSPC);
	}
nospace:
	if (ratecheck(&fsfull_last, &fserr_interval)) {
		ffs_fserr(fs, cred->cr_uid, "file system full");
		uprintf("\n%s: write failed, file system is full\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Reallocate a fragment to a bigger size
 *
 * The number and size of the old block is given, and a preference
 * and new size is also specified. The allocator attempts to extend
 * the original block. Failing that, the regular block allocator is
 * invoked to get an appropriate block.
 */
int
ffs_realloccg(struct inode *ip, daddr_t lbprev, daddr_t bpref, int osize,
    int nsize, int flags, struct ucred *cred, struct buf **bpp, daddr_t *blknop)
{
	static struct timeval fsfull_last;
	struct fs *fs;
	struct buf *bp = NULL;
	daddr_t quota_updated = 0;
	int cg, request, error;
	daddr_t bprev, bno;

	if (bpp != NULL)
		*bpp = NULL;
	fs = ip->i_fs;
#ifdef DIAGNOSTIC
	if ((u_int)osize > fs->fs_bsize || fragoff(fs, osize) != 0 ||
	    (u_int)nsize > fs->fs_bsize || fragoff(fs, nsize) != 0) {
		printf(
		    "dev = 0x%x, bsize = %d, osize = %d, nsize = %d, fs = %s\n",
		    ip->i_dev, fs->fs_bsize, osize, nsize, fs->fs_fsmnt);
		panic("ffs_realloccg: bad size");
	}
	if (cred == NOCRED)
		panic("ffs_realloccg: missing credential");
#endif /* DIAGNOSTIC */
	if (cred->cr_uid != 0 && freespace(fs, fs->fs_minfree) <= 0)
		goto nospace;

	bprev = DIP(ip, db[lbprev]);

	if (bprev == 0) {
		printf("dev = 0x%x, bsize = %d, bprev = %lld, fs = %s\n",
		    ip->i_dev, fs->fs_bsize, (long long)bprev, fs->fs_fsmnt);
		panic("ffs_realloccg: bad bprev");
	}

	/*
	 * Allocate the extra space in the buffer.
	 */
	if (bpp != NULL) {
		if ((error = bread(ITOV(ip), lbprev, fs->fs_bsize, &bp)) != 0)
			goto error;
		buf_adjcnt(bp, osize);
	}

	if ((error = ufs_quota_alloc_blocks(ip, btodb(nsize - osize), cred))
	    != 0)
		goto error;

	quota_updated = btodb(nsize - osize);

	/*
	 * Check for extension in the existing location.
	 */
	cg = dtog(fs, bprev);
	if ((bno = ffs_fragextend(ip, cg, bprev, osize, nsize)) != 0) {
		DIP_ADD(ip, blocks, btodb(nsize - osize));
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (bpp != NULL) {
			if (bp->b_blkno != fsbtodb(fs, bno))
				panic("ffs_realloccg: bad blockno");
#ifdef DIAGNOSTIC
			if (nsize > bp->b_bufsize)
				panic("ffs_realloccg: small buf");
#endif
			buf_adjcnt(bp, nsize);
			bp->b_flags |= B_DONE;
			memset(bp->b_data + osize, 0, nsize - osize);
			*bpp = bp;
		}
		if (blknop != NULL) {
			*blknop = bno;
		}
		return (0);
	}
	/*
	 * Allocate a new disk location.
	 */
	if (bpref >= fs->fs_size)
		bpref = 0;
	switch (fs->fs_optim) {
	case FS_OPTSPACE:
		/*
		 * Allocate an exact sized fragment. Although this makes
		 * best use of space, we will waste time relocating it if
		 * the file continues to grow. If the fragmentation is
		 * less than half of the minimum free reserve, we choose
		 * to begin optimizing for time.
		 */
		request = nsize;
		if (fs->fs_minfree < 5 ||
		    fs->fs_cstotal.cs_nffree >
		    fs->fs_dsize * fs->fs_minfree / (2 * 100))
			break;
		fs->fs_optim = FS_OPTTIME;
		break;
	case FS_OPTTIME:
		/*
		 * At this point we have discovered a file that is trying to
		 * grow a small fragment to a larger fragment. To save time,
		 * we allocate a full sized block, then free the unused portion.
		 * If the file continues to grow, the `ffs_fragextend' call
		 * above will be able to grow it in place without further
		 * copying. If aberrant programs cause disk fragmentation to
		 * grow within 2% of the free reserve, we choose to begin
		 * optimizing for space.
		 */
		request = fs->fs_bsize;
		if (fs->fs_cstotal.cs_nffree <
		    fs->fs_dsize * (fs->fs_minfree - 2) / 100)
			break;
		fs->fs_optim = FS_OPTSPACE;
		break;
	default:
		printf("dev = 0x%x, optim = %d, fs = %s\n",
		    ip->i_dev, fs->fs_optim, fs->fs_fsmnt);
		panic("ffs_realloccg: bad optim");
		/* NOTREACHED */
	}
	bno = ffs_hashalloc(ip, cg, bpref, request, flags, ffs_alloccg);
	if (bno <= 0)
		goto nospace;

	(void) uvm_vnp_uncache(ITOV(ip));

	/* WAPBL: mark previous block as deallocated in the log */
	if (ip->i_ump->um_mountp->mnt_wapbl && ITOV(ip)->v_type != VREG) {
		UFS_WAPBL_REGISTER_DEALLOCATION(ip->i_ump->um_mountp,
		    fsbtodb(fs, bprev), osize);
	} else {
		if (!DOINGSOFTDEP(ITOV(ip)))
			ffs_blkfree(ip, bprev, (long)osize);
	}
	if (nsize < request) {
		/* WAPBL: mark previous fragment as deallocated in the log */
		if (ip->i_ump->um_mountp->mnt_wapbl &&
		    ITOV(ip)->v_type != VREG) {
			UFS_WAPBL_REGISTER_DEALLOCATION(ip->i_ump->um_mountp,
			    fsbtodb(fs, (bno + numfrags(fs, nsize))),
			    request - nsize);
		} else {
			/* XXX pedro: no softdep? */
			ffs_blkfree(ip, bno + numfrags(fs, nsize),
			    (long)(request - nsize));
		}
	}
	DIP_ADD(ip, blocks, btodb(nsize - osize));
	ip->i_flag |= IN_CHANGE | IN_UPDATE;
	if (bpp != NULL) {
		bp->b_blkno = fsbtodb(fs, bno);
#ifdef DIAGNOSTIC
		if (nsize > bp->b_bufsize)
			panic("ffs_realloccg: small buf 2");
#endif
		buf_adjcnt(bp, nsize);
		bp->b_flags |= B_DONE;
		memset(bp->b_data + osize, 0, nsize - osize);
		*bpp = bp;
	}
	if (blknop != NULL) {
		*blknop = bno;
	}
	return (0);

nospace:
	if (ratecheck(&fsfull_last, &fserr_interval)) {
		ffs_fserr(fs, cred->cr_uid, "file system full");
		uprintf("\n%s: write failed, file system is full\n",
		    fs->fs_fsmnt);
	}
	error = ENOSPC;

error:
	if (bp != NULL) {
		brelse(bp);
		bp = NULL;
	}

 	/*
	 * Restore user's disk quota because allocation failed.
	 */
	if (quota_updated != 0)
		(void)ufs_quota_free_blocks(ip, quota_updated, cred);

	return error;
}

/*
 * Reallocate a sequence of blocks into a contiguous sequence of blocks.
 *
 * The vnode and an array of buffer pointers for a range of sequential
 * logical blocks to be made contiguous are given. The allocator attempts
 * to find a range of sequential blocks starting as close as possible to
 * an fs_rotdelay offset from the end of the allocation for the logical
 * block immediately preceding the current range. If successful, the
 * physical block numbers in the buffer pointers and in the inode are
 * changed to reflect the new allocation. If unsuccessful, the allocation
 * is left unchanged. The success in doing the reallocation is returned.
 * Note that the error return is not reflected back to the user. Rather
 * the previous block allocation will be used.
 */

int doasyncfree = 1;
int doreallocblks = 1;
int prtrealloc = 0;

int
ffs1_reallocblks(void *v)
{
	struct vop_reallocblks_args *ap = v;
	const struct fs *fs;
	struct inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp;
	int32_t *bap, *sbap, *ebap = NULL;
	struct cluster_save *buflist;
	daddr_t start_lbn, end_lbn, soff, newblk, blkno;
	struct indir start_ap[NIADDR + 1], end_ap[NIADDR + 1];
	const struct indir *idp;
	int i, len, start_lvl, end_lvl, pref, ssize;

	vp = ap->a_vp;
	ip = VTOI(vp);
	fs = ip->i_fs;
	if (fs->fs_contigsumsize <= 0)
		return (ENOSPC);
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;

#ifdef DIAGNOSTIC
	for (i = 0; i < len; i++)
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs1_reallocblks: unallocated block 1");

	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("ffs1_reallocblks: non-logical cluster");

	blkno = buflist->bs_children[0]->b_blkno;
	ssize = fsbtodb(fs, fs->fs_frag);
	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_blkno != blkno + (i * ssize))
			panic("ffs1_reallocblks: non-physical cluster %d", i);
#endif
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    dtog(fs, dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_ffs1_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (int32_t *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * Find the preferred location for the cluster.
	 */
	pref = ffs1_blkpref(ip, start_lbn, soff, 0, sbap);
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef DIAGNOSTIC
		if (start_lvl > 1 &&
		    start_ap[start_lvl-1].in_lbn == idp->in_lbn)
			panic("ffs1_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, &ebp))
			goto fail;
		ebap = (int32_t *)ebp->b_data;
	}
	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = ffs_hashalloc(ip, dtog(fs, pref), pref, len, 0,
	    ffs_clusteralloc)) == 0)
		goto fail;
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("realloc: ino %u, lbns %lld-%lld\n\told:", ip->i_number,
		    (long long)start_lbn, (long long)end_lbn);
#endif
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef DIAGNOSTIC
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs1_reallocblks: unallocated block 2");
		if (dbtofsb(fs, buflist->bs_children[i]->b_blkno) != *bap)
			panic("ffs1_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %d,", *bap);
#endif
		if (DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_ffs1_db[0] && i < ssize)
				softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}

		*bap++ = blkno;
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_ffs1_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree) {
			UFS_UPDATE(ip, MNT_WAIT);
		}
	}
	if (ssize < len) {
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (ip->i_ump->um_mountp->mnt_wapbl &&
		    ITOV(ip)->v_type != VREG) {
		    	UFS_WAPBL_REGISTER_DEALLOCATION(ip->i_ump->um_mountp,
			    buflist->bs_children[i]->b_blkno, fs->fs_bsize);
		} else if (!DOINGSOFTDEP(vp))
			ffs_blkfree(ip,
			    dbtofsb(fs, buflist->bs_children[i]->b_blkno),
			    fs->fs_bsize);
		buflist->bs_children[i]->b_blkno = fsbtodb(fs, blkno);
#ifdef DIAGNOSTIC
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs1_reallocblks: unallocated block 3");
		if (prtrealloc)
			printf(" %lld,", (long long)blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		printf("\n");
	}
#endif
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_ffs1_db[0])
		brelse(sbp);
	return (ENOSPC);
}

#ifdef FFS2
int
ffs2_reallocblks(void *v)
{
	struct vop_reallocblks_args *ap = v;
	const struct fs *fs;
	struct inode *ip;
	struct vnode *vp;
	struct buf *sbp, *ebp;
	daddr_t *bap, *sbap, *ebap = NULL;
	struct cluster_save *buflist;
	daddr_t start_lbn, end_lbn;
	daddr_t soff, newblk, blkno, pref;
	struct indir start_ap[NIADDR + 1], end_ap[NIADDR + 1];
	const struct indir *idp;
	int i, len, start_lvl, end_lvl, ssize;

	vp = ap->a_vp;
	ip = VTOI(vp);
	fs = ip->i_fs;

	if (fs->fs_contigsumsize <= 0)
		return (ENOSPC);

	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;

#ifdef DIAGNOSTIC
	for (i = 0; i < len; i++)
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs2_reallocblks: unallocated block 1");

	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("ffs2_reallocblks: non-logical cluster");

	blkno = buflist->bs_children[0]->b_blkno;
	ssize = fsbtodb(fs, fs->fs_frag);

	for (i = 1; i < len - 1; i++)
		if (buflist->bs_children[i]->b_blkno != blkno + (i * ssize))
			panic("ffs2_reallocblks: non-physical cluster %d", i);
#endif

	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    dtog(fs, dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);

	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_din2->di_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (daddr_t *)sbp->b_data;
		soff = idp->in_off;
	}

	/*
	 * If the block range spans two block maps, get the second map.
	 */
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#ifdef DIAGNOSTIC
		if (start_ap[start_lvl-1].in_lbn == idp->in_lbn)
			panic("ffs2_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (bread(vp, idp->in_lbn, (int)fs->fs_bsize, &ebp))
			goto fail;
		ebap = (daddr_t *)ebp->b_data;
	}

	/*
	 * Find the preferred location for the cluster.
	 */
	pref = ffs2_blkpref(ip, start_lbn, soff, 0, sbap);

	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = ffs_hashalloc(ip, dtog(fs, pref), pref,
	    len, 0, ffs_clusteralloc)) == 0)
		goto fail;

	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("realloc: ino %u, lbns %lld-%lld\n\told:", ip->i_number,
		    (long long)start_lbn, (long long)end_lbn);
#endif

	blkno = newblk;

	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (i == ssize) {
			bap = ebap;
			soff = -i;
		}
#ifdef DIAGNOSTIC
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs2_reallocblks: unallocated block 2");
		if (dbtofsb(fs, buflist->bs_children[i]->b_blkno) != *bap)
			panic("ffs2_reallocblks: alloc mismatch");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %lld,", (long long)*bap);
#endif
		if (DOINGSOFTDEP(vp)) {
			if (sbap == &ip->i_din2->di_db[0] && i < ssize)
				softdep_setup_allocdirect(ip, start_lbn + i,
				    blkno, *bap, fs->fs_bsize, fs->fs_bsize,
				    buflist->bs_children[i]);
			else
				softdep_setup_allocindir_page(ip, start_lbn + i,
				    i < ssize ? sbp : ebp, soff + i, blkno,
				    *bap, buflist->bs_children[i]);
		}
		*bap++ = blkno;
	}

	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_din2->di_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree)
			ffs_update(ip, MNT_WAIT);
	}

	if (ssize < len) {
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	}

	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
#ifdef DEBUG
	if (prtrealloc)
		printf("\n\tnew:");
#endif
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->fs_frag) {
		if (ip->i_ump->um_mountp->mnt_wapbl &&
		    ITOV(ip)->v_type != VREG) {
		    	UFS_WAPBL_REGISTER_DEALLOCATION(ip->i_ump->um_mountp,
			    buflist->bs_children[i]->b_blkno, fs->fs_bsize);
		} else if (!DOINGSOFTDEP(vp))
			ffs_blkfree(ip, dbtofsb(fs,
			    buflist->bs_children[i]->b_blkno), fs->fs_bsize);
		buflist->bs_children[i]->b_blkno = fsbtodb(fs, blkno);
#ifdef DIAGNOSTIC
		if (!ffs_checkblk(ip,
		   dbtofsb(fs, buflist->bs_children[i]->b_blkno), fs->fs_bsize))
			panic("ffs2_reallocblks: unallocated block 3");
#endif
#ifdef DEBUG
		if (prtrealloc)
			printf(" %lld,", (long long)blkno);
#endif
	}
#ifdef DEBUG
	if (prtrealloc) {
		prtrealloc--;
		printf("\n");
	}
#endif

	return (0);

fail:
	if (ssize < len)
		brelse(ebp);

	if (sbap != &ip->i_din2->di_db[0])
		brelse(sbp);

	return (ENOSPC);
}
#endif /* FFS2 */

int
ffs_reallocblks(void *v)
{
#ifdef FFS2
	struct vop_reallocblks_args *ap = v;
#endif

	if (!doreallocblks)
		return (ENOSPC);

#ifdef FFS2
	if (VTOI(ap->a_vp)->i_ump->um_fstype == UM_UFS2)
		return (ffs2_reallocblks(v));
#endif

	return (ffs1_reallocblks(v));
}

/*
 * Allocate an inode in the file system.
 *
 * If allocating a directory, use ffs_dirpref to select the inode.
 * If allocating in a directory, the following hierarchy is followed:
 *   1) allocate the preferred inode.
 *   2) allocate an inode in the same cylinder group.
 *   3) quadratically rehash into other cylinder groups, until an
 *      available inode is located.
 * If no inode preference is given the following hierarchy is used
 * to allocate an inode:
 *   1) allocate an inode in cylinder group 0.
 *   2) quadratically rehash into other cylinder groups, until an
 *      available inode is located.
 */
int
ffs_inode_alloc(struct inode *pip, mode_t mode, struct ucred *cred,
    struct vnode **vpp)
{
	static struct timeval fsnoinodes_last;
	struct vnode *pvp = ITOV(pip);
	struct fs *fs;
	struct inode *ip;
	ufsino_t ino, ipref;
	int cg, error;

	UFS_WAPBL_JUNLOCK_ASSERT(pvp->v_mount);

	*vpp = NULL;
	fs = pip->i_fs;

	error = UFS_WAPBL_BEGIN(pvp->v_mount);
	if (error)
		return error;

	if (fs->fs_cstotal.cs_nifree == 0)
		goto noinodes;

	if ((mode & IFMT) == IFDIR)
		ipref = ffs_dirpref(pip);
	else
		ipref = pip->i_number;
	if (ipref >= fs->fs_ncg * fs->fs_ipg)
		ipref = 0;
	cg = ino_to_cg(fs, ipref);

	/*
	 * Track number of dirs created one after another
	 * in a same cg without intervening by files.
	 */
	if ((mode & IFMT) == IFDIR) {
		if (fs->fs_contigdirs[cg] < 255)
			fs->fs_contigdirs[cg]++;
	} else {
		if (fs->fs_contigdirs[cg] > 0)
			fs->fs_contigdirs[cg]--;
	}
	ino = (ufsino_t)ffs_hashalloc(pip, cg, ipref, mode, 0, ffs_nodealloccg);
	if (ino == 0)
		goto noinodes;
	UFS_WAPBL_END(pvp->v_mount);
	error = VFS_VGET(pvp->v_mount, ino, vpp);
	if (error) {
		int err;
		err = UFS_WAPBL_BEGIN(pvp->v_mount);
		if (err == 0) {
			ffs_inode_free(pip, ino, mode);
			UFS_WAPBL_END(pvp->v_mount);
		}
		return (error);
	}

	ip = VTOI(*vpp);

	if (DIP(ip, mode)) {
		printf("mode = 0%o, inum = %u, fs = %s\n",
		    DIP(ip, mode), ip->i_number, fs->fs_fsmnt);
		panic("ffs_valloc: dup alloc");
	}

	if (DIP(ip, blocks)) {
		printf("free inode %s/%d had %lld blocks\n",
		    fs->fs_fsmnt, ino, (long long)DIP(ip, blocks));
		DIP_ASSIGN(ip, blocks, 0);
	}

	DIP_ASSIGN(ip, flags, 0);

	/*
	 * Set up a new generation number for this inode.
	 * XXX - just increment for now, this is wrong! (millert)
	 *       Need a way to preserve randomization.
	 */
	if (DIP(ip, gen) != 0)
		DIP_ADD(ip, gen, 1);
	if (DIP(ip, gen) == 0)
		DIP_ASSIGN(ip, gen, arc4random() & INT_MAX);

	if (DIP(ip, gen) == 0 || DIP(ip, gen) == -1)
		DIP_ASSIGN(ip, gen, 1);	/* Shouldn't happen */

	return (0);

noinodes:
	UFS_WAPBL_END(pvp->v_mount);
	if (ratecheck(&fsnoinodes_last, &fserr_interval)) {
		ffs_fserr(fs, cred->cr_uid, "out of inodes");
		uprintf("\n%s: create/symlink failed, no inodes free\n",
		    fs->fs_fsmnt);
	}
	return (ENOSPC);
}

/*
 * Find a cylinder group to place a directory.
 *
 * The policy implemented by this algorithm is to allocate a
 * directory inode in the same cylinder group as its parent
 * directory, but also to reserve space for its files inodes
 * and data. Restrict the number of directories which may be
 * allocated one after another in the same cylinder group
 * without intervening allocation of files.
 *
 * If we allocate a first level directory then force allocation
 * in another cylinder group.
 */
ufsino_t
ffs_dirpref(const struct inode *pip)
{
	const struct fs *fs;
	int	cg, prefcg, dirsize, cgsize;
	int	avgifree, avgbfree, avgndir, curdirsize;
	int	minifree, minbfree, maxndir;
	int	mincg, minndir;
	int	maxcontigdirs;

	fs = pip->i_fs;

	avgifree = fs->fs_cstotal.cs_nifree / fs->fs_ncg;
	avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
	avgndir = fs->fs_cstotal.cs_ndir / fs->fs_ncg;

	/*
	 * Force allocation in another cg if creating a first level dir.
	 */
	if (ITOV(pip)->v_flag & VROOT) {
		prefcg = (arc4random() & INT_MAX) % fs->fs_ncg;
		mincg = prefcg;
		minndir = fs->fs_ipg;
		for (cg = prefcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		for (cg = 0; cg < prefcg; cg++)
			if (fs->fs_cs(fs, cg).cs_ndir < minndir &&
			    fs->fs_cs(fs, cg).cs_nifree >= avgifree &&
			    fs->fs_cs(fs, cg).cs_nbfree >= avgbfree) {
				mincg = cg;
				minndir = fs->fs_cs(fs, cg).cs_ndir;
			}
		cg = mincg;
		goto end;
	} else
		prefcg = ino_to_cg(fs, pip->i_number);

	/*
	 * Count various limits which used for
	 * optimal allocation of a directory inode.
	 */
	maxndir = min(avgndir + fs->fs_ipg / 16, fs->fs_ipg);
	minifree = avgifree - (avgifree / 4);
	if (minifree < 1)
		minifree = 1;
	minbfree = avgbfree - (avgbfree / 4);
	if (minbfree < 1)
		minbfree = 1;

	cgsize = fs->fs_fsize * fs->fs_fpg;
	dirsize = fs->fs_avgfilesize * fs->fs_avgfpdir;
	curdirsize = avgndir ? (cgsize - avgbfree * fs->fs_bsize) / avgndir : 0;
	if (dirsize < curdirsize)
		dirsize = curdirsize;
	if (dirsize <= 0)
		maxcontigdirs = 0;		/* dirsize overflowed */
	else
		maxcontigdirs = min(avgbfree * fs->fs_bsize  / dirsize, 255);
	if (fs->fs_avgfpdir > 0)
		maxcontigdirs = min(maxcontigdirs,
				    fs->fs_ipg / fs->fs_avgfpdir);
	if (maxcontigdirs == 0)
		maxcontigdirs = 1;

	/*
	 * Limit number of dirs in one cg and reserve space for 
	 * regular files, but only if we have no deficit in
	 * inodes or space.
	 *
	 * We are trying to find a suitable cylinder group nearby
	 * our preferred cylinder group to place a new directory.
	 * We scan from our preferred cylinder group forward looking
	 * for a cylinder group that meets our criterion. If we get
	 * to the final cylinder group and do not find anything,
	 * we start scanning backwards from our preferred cylinder
	 * group. The ideal would be to alternate looking forward
	 * and backward, but tha tis just too complex to code for
	 * the gain it would get. The most likely place where the
	 * backward scan would take effect is when we start near
	 * the end of the filesystem and do not find anything from
	 * where we are to the end. In that case, scanning backward
	 * will likely find us a suitable cylinder group much closer
	 * to our desired location than if we were to start scanning
	 * forward from the beginning for the filesystem.
	 */
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
	    	    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				goto end;
		}
	for (cg = prefcg - 1; cg >= 0; cg--)
		if (fs->fs_cs(fs, cg).cs_ndir < maxndir &&
		    fs->fs_cs(fs, cg).cs_nifree >= minifree &&
	    	    fs->fs_cs(fs, cg).cs_nbfree >= minbfree) {
			if (fs->fs_contigdirs[cg] < maxcontigdirs)
				goto end;
		}
	/*
	 * This is a backstop when we have deficit in space.
	 */
	for (cg = prefcg; cg < fs->fs_ncg; cg++)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			goto end;
	for (cg = prefcg - 1; cg >= 0; cg--)
		if (fs->fs_cs(fs, cg).cs_nifree >= avgifree)
			goto end;
end:
	return ((ufsino_t)(fs->fs_ipg * cg));
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks. Each additional section contains fs_maxbpg blocks.
 *
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. The first indirect is allocated immediately following the last
 * direct block and the data blocks for the first indirect immediately
 * follow it.
 *
 * If no blocks have been allocated in any other section, the indirect
 * block(s) are allocated in the same cylinder group as its inode in an
 * area reserved immediately following the inode blocks. The policy for
 * the data blocks is to place them in a cylinder group with a greater than
 * average number of free blocks. An appropriate cylinder group is found
 * by using a rotor that sweeps the cylinder groups. When a new group of
 * blocks is needed, the sweep begins in the cylinder group following the
 * cylinder group from which the previous allocation was made. The sweep
 * continues until a cylinder group with greater than the average number
 * of free blocks is found. If the allocation is for the first block in an
 * indirect block, the information on the previous allocation is unavailable;
 * here a best guess is made based upon the logical block number being
 * allocated.
 */
int32_t
ffs1_blkpref(const struct inode *ip, daddr_t lbn, int indx, int flags,
    const int32_t *bap)
{
	const struct fs *fs;
	int cg, inocg, avgbfree, startcg;
	uint32_t pref;

	KASSERT(indx <= 0 || bap != NULL);
	fs = ip->i_fs;

	/*
	 * If allocating a contiguous file with B_CONTIG, use the hints
	 * in the inode extentions to return the desired block.
	 *
	 * For metadata (indirect blocks) return the address of where
	 * the first indirect block resides - we'll scan for the next
	 * available slot if we need to allocate more than one indirect
	 * block.  For data, return the address of the actual block
	 * relative to the address of the first data block.
	 */
	if (flags & B_CONTIG) {
		KASSERT(ip->i_ffs_first_data_blk != 0);
		KASSERT(ip->i_ffs_first_indir_blk != 0);
		if (flags & B_METAONLY)
			return ip->i_ffs_first_indir_blk;
		else
			return ip->i_ffs_first_data_blk + blkstofrags(fs, lbn);
	}

	/*
	 * Allocation of indirect blocks is indicated by passing negative
	 * values in indx: -1 for single indirect, -2 for double indirect,
	 * -3 for triple indirect. As noted below, we attempt to allocate
	 * the first indirect inline with the file data. For all later
	 * indirect blocks, the data is often allocated in other cylinder
	 * groups. However to speed random file access and to speed up
	 * fsck, the filesystem reserves the first fs_metaspace blocks
	 * (typically half of fs_minfree) of the data area of each cylinder
	 * group to hold these later indirect blocks.
	 */
	inocg = ino_to_cg(fs, ip->i_number);
	if (indx < 0) {
		/*
		 * Our preference for indirect blocks is the zone at the
		 * beginning of the inode's cylinder group data area that
		 * we try to reserve for indirect blocks.
		 */
		pref = cgmeta(fs, inocg);
		/*
		 * If we are allocating the first indirect block, try to
		 * place it immediately following the last direct block.
		 */
		if (indx == -1 && lbn < NDADDR + NINDIR(fs) &&
		    ip->i_din1->di_db[NDADDR - 1] != 0)
			pref = ip->i_din1->di_db[NDADDR - 1] + fs->fs_frag;
		return (pref);
	}
	/*
	 * If we are allocating the first data block in the first indirect
	 * block and the indirect has been allocated in the data block area,
	 * try to place it immediately following the indirect block.
	 */
	if (lbn == NDADDR) {
		pref = ip->i_din1->di_ib[0];
		if (pref != 0 && pref >= cgdata(fs, inocg) &&
		    pref < cgbase(fs, inocg + 1))
			return (pref + fs->fs_frag);
	}
	/*
	 * If we are the beginning of a file, or we have already allocated
	 * the maximum number of blocks per cylinder group, or we do not
	 * have a block allocated immediately preceding us, then we need
	 * to decide where to start allocating new blocks.
	 */
	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		/*
		 * If we are allocating a directory data block, we want
		 * to place it in the metadata area.
		 */
		if ((DIP(ip, mode) & IFMT) == IFDIR)
			return (cgmeta(fs, inocg));
		/*
		 * Until we fill all the direct and all the first indirect's
		 * blocks, we try to allocate in the data area of the inode's
		 * cylinder group.
		 */
		if (lbn < NDADDR + NINDIR(fs))
			return (cgdata(fs, inocg));
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg = inocg + lbn / fs->fs_maxbpg;
		else
			startcg = dtog(fs, bap[indx - 1]) + 1;
		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;
		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree)
				return (cgdata(fs, cg));
		for (cg = 0; cg <= startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree)
				return (cgdata(fs, cg));
		return (0);
	}
	/*
	 * Otherwise, we just always try to lay things out contiguously.
	 */
	return (bap[indx - 1] + fs->fs_frag);
}

/*
 * Same as above, for UFS2.
 */
#ifdef FFS2
int64_t
ffs2_blkpref(const struct inode *ip, daddr_t lbn, int indx, int flags,
    const int64_t *bap)
{
	const struct fs *fs;
	int cg, inocg, avgbfree, startcg;
	uint64_t pref;

	KASSERT(indx <= 0 || bap != NULL);
	fs = ip->i_fs;

	/*
	 * If allocating a contiguous file with B_CONTIG, use the hints
	 * in the inode extentions to return the desired block.
	 *
	 * For metadata (indirect blocks) return the address of where
	 * the first indirect block resides - we'll scan for the next
	 * available slot if we need to allocate more than one indirect
	 * block.  For data, return the address of the actual block
	 * relative to the address of the first data block.
	 */
	if (flags & B_CONTIG) {
		KASSERT(ip->i_ffs_first_data_blk != 0);
		KASSERT(ip->i_ffs_first_indir_blk != 0);
		if (flags & B_METAONLY)
			return ip->i_ffs_first_indir_blk;
		else
			return ip->i_ffs_first_data_blk + blkstofrags(fs, lbn);
	}

	/*
	 * Allocation of indirect blocks is indicated by passing negative
	 * values in indx: -1 for single indirect, -2 for double indirect,
	 * -3 for triple indirect. As noted below, we attempt to allocate
	 * the first indirect inline with the file data. For all later
	 * indirect blocks, the data is often allocated in other cylinder
	 * groups. However to speed random file access and to speed up
	 * fsck, the filesystem reserves the first fs_metaspace blocks
	 * (typically half of fs_minfree) of the data area of each cylinder
	 * group to hold these later indirect blocks.
	 */
	inocg = ino_to_cg(fs, ip->i_number);
	if (indx < 0) {
		/*
		 * Our preference for indirect blocks is the zone at the
		 * beginning of the inode's cylinder group data area that
		 * we try to reserve for indirect blocks.
		 */
		pref = cgmeta(fs, inocg);
		/*
		 * If we are allocating the first indirect block, try to
		 * place it immediately following the last direct block.
		 */
		if (indx == -1 && lbn < NDADDR + NINDIR(fs) &&
		    ip->i_din2->di_db[NDADDR - 1] != 0)
			pref = ip->i_din2->di_db[NDADDR - 1] + fs->fs_frag;
		return (pref);
	}
	/*
	 * If we are allocating the first data block in the first indirect
	 * block and the indirect has been allocated in the data block area,
	 * try to place it immediately following the indirect block.
	 */
	if (lbn == NDADDR) {
		pref = ip->i_din2->di_ib[0];
		if (pref != 0 && pref >= cgdata(fs, inocg) &&
		    pref < cgbase(fs, inocg + 1))
			return (pref + fs->fs_frag);
	}
	/*
	 * If we are the beginning of a file, or we have already allocated
	 * the maximum number of blocks per cylinder group, or we do not
	 * have a block allocated immediately preceding us, then we need
	 * to decide where to start allocating new blocks.
	 */

	if (indx % fs->fs_maxbpg == 0 || bap[indx - 1] == 0) {
		/*
		 * If we are allocating a directory data block, we want
		 * to place it in the metadata area.
		 */
		if ((DIP(ip, mode) & IFMT) == IFDIR)
			return (cgmeta(fs, inocg));
		/*
		 * Until we fill all the direct and all the first indirect's
		 * blocks, we try to allocate in the data area of the inode's
		 * cylinder group.
		 */
		if (lbn < NDADDR + NINDIR(fs))
			return (cgdata(fs, inocg));
		/*
		 * Find a cylinder with greater than average number of
		 * unused data blocks.
		 */
		if (indx == 0 || bap[indx - 1] == 0)
			startcg = inocg + lbn / fs->fs_maxbpg;
		else
			startcg = dtog(fs, bap[indx - 1] + 1);

		startcg %= fs->fs_ncg;
		avgbfree = fs->fs_cstotal.cs_nbfree / fs->fs_ncg;

		for (cg = startcg; cg < fs->fs_ncg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree)
				return (cgbase(fs, cg) + fs->fs_frag);

		for (cg = 0; cg < startcg; cg++)
			if (fs->fs_cs(fs, cg).cs_nbfree >= avgbfree)
				return (cgbase(fs, cg) + fs->fs_frag);

		return (0);
	}

	/*
	 * Otherwise, we just always try to lay things out contiguously.
	 */
	return (bap[indx - 1] + fs->fs_frag);
}
#endif /* FFS2 */

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadratically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 */
/*VARARGS5*/
daddr_t
ffs_hashalloc(struct inode *ip, int cg, daddr_t pref, int size, int flags,
    daddr_t (*allocator)(const struct inode *, int, daddr_t, int, int))
{
	struct fs *fs;
	daddr_t result;
	int i, icg = cg;

	fs = ip->i_fs;
	/*
	 * 1: preferred cylinder group
	 */
	result = (*allocator)(ip, cg, pref, size, flags);
	if (result)
		return (result);

	if (flags & B_CONTIG)
		return (result);

	/*
	 * 2: quadratic rehash
	 */
	for (i = 1; i < fs->fs_ncg; i *= 2) {
		cg += i;
		if (cg >= fs->fs_ncg)
			cg -= fs->fs_ncg;
		result = (*allocator)(ip, cg, 0, size, flags);
		if (result)
			return (result);
	}
	/*
	 * 3: brute force search
	 * Note that we start at i == 2, since 0 was checked initially,
	 * and 1 is always checked in the quadratic rehash.
	 */
	cg = (icg + 2) % fs->fs_ncg;
	for (i = 2; i < fs->fs_ncg; i++) {
		result = (*allocator)(ip, cg, 0, size, flags);
		if (result)
			return (result);
		cg++;
		if (cg == fs->fs_ncg)
			cg = 0;
	}
	return (0);
}

struct buf *
ffs_cgread(const struct fs *fs, struct vnode *devvp, int cg)
{
	struct buf *bp;

	if (bread(devvp, fsbtodb(fs, cgtod(fs, cg)),
	    (int)fs->fs_cgsize, &bp)) {
		brelse(bp);
		return (NULL);
	}

	if (!cg_chkmagic((struct cg *)bp->b_data)) {
		brelse(bp);
		return (NULL);
	}

	return bp;
}

/*
 * Determine whether a fragment can be extended.
 *
 * Check to see if the necessary fragments are available, and
 * if they are, allocate them.
 */
daddr_t
ffs_fragextend(const struct inode *ip, int cg, daddr_t bprev, int osize,
    int nsize)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	daddr_t bno;
	int i, frags, bbase;

	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nffree < numfrags(fs, nsize - osize))
		return (0);
	frags = numfrags(fs, nsize);
	bbase = fragnum(fs, bprev);
	if (bbase > fragnum(fs, (bprev + frags - 1))) {
		/* cannot extend across a block boundary */
		return (0);
	}

	if (!(bp = ffs_cgread(fs, ip->i_devvp, cg)))
		return (0);

	cgp = (struct cg *)bp->b_data;
	cgp->cg_ffs2_time = cgp->cg_time = time_second;

	bno = dtogd(fs, bprev);
	for (i = numfrags(fs, osize); i < frags; i++)
		if (isclr(cg_blksfree(cgp), bno + i)) {
			brelse(bp);
			return (0);
		}
	/*
	 * the current fragment can be extended
	 * deduct the count on fragment being extended into
	 * increase the count on the remaining fragment (if any)
	 * allocate the extended piece
	 */
	for (i = frags; i < fs->fs_frag - bbase; i++)
		if (isclr(cg_blksfree(cgp), bno + i))
			break;
	cgp->cg_frsum[i - numfrags(fs, osize)]--;
	if (i != frags)
		cgp->cg_frsum[i - frags]++;
	for (i = numfrags(fs, osize); i < frags; i++) {
		clrbit(cg_blksfree(cgp), bno + i);
		cgp->cg_cs.cs_nffree--;
		fs->fs_cstotal.cs_nffree--;
		fs->fs_cs(fs, cg).cs_nffree--;
	}
	fs->fs_fmod = 1;
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, fs, bprev);

	bdwrite(bp);
	return (bprev);
}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the appropriate size is available,
 * and if it is, allocate it.
 */
daddr_t
ffs_alloccg(const struct inode *ip, int cg, daddr_t bpref, int size, int flags)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	daddr_t bno, blkno;
	int i, frags, allocsiz;

	fs = ip->i_fs;
	if (fs->fs_cs(fs, cg).cs_nbfree == 0 && size == fs->fs_bsize)
		return (0);

	if (!(bp = ffs_cgread(fs, ip->i_devvp, cg)))
		return (0);

	cgp = (struct cg *)bp->b_data;
	if (cgp->cg_cs.cs_nbfree == 0 && size == fs->fs_bsize) {
		brelse(bp);
		return (0);
	}

	cgp->cg_ffs2_time = cgp->cg_time = time_second;

	if (size == fs->fs_bsize) {
		/* allocate and return a complete data block */
		bno = ffs_alloccgblk(ip, bp, bpref, flags);
		bdwrite(bp);
		return (bno);
	}
	/*
	 * check to see if any fragments are already available
	 * allocsiz is the size which will be allocated, hacking
	 * it down to a smaller size if necessary
	 */
	frags = numfrags(fs, size);
	for (allocsiz = frags; allocsiz < fs->fs_frag; allocsiz++)
		if (cgp->cg_frsum[allocsiz] != 0)
			break;
	if (allocsiz == fs->fs_frag) {
		/*
		 * no fragments were available, so a block will be
		 * allocated, and hacked up
		 */
		if (cgp->cg_cs.cs_nbfree == 0) {
			brelse(bp);
			return (0);
		}
		bno = ffs_alloccgblk(ip, bp, bpref, flags);
		bpref = dtogd(fs, bno);
		for (i = frags; i < fs->fs_frag; i++)
			setbit(cg_blksfree(cgp), bpref + i);
		i = fs->fs_frag - frags;
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		fs->fs_fmod = 1;
		cgp->cg_frsum[i]++;
		bdwrite(bp);
		return (bno);
	}
	bno = ffs_mapsearch(fs, cgp, bpref, allocsiz);
	if (bno < 0) {
		brelse(bp);
		return (0);
	}

	for (i = 0; i < frags; i++)
		clrbit(cg_blksfree(cgp), bno + i);
	cgp->cg_cs.cs_nffree -= frags;
	fs->fs_cstotal.cs_nffree -= frags;
	fs->fs_cs(fs, cg).cs_nffree -= frags;
	fs->fs_fmod = 1;
	cgp->cg_frsum[allocsiz]--;
	if (frags != allocsiz)
		cgp->cg_frsum[allocsiz - frags]++;

	blkno = cgbase(fs, cg) + bno;
	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, fs, blkno);
	bdwrite(bp);
	return (blkno);
}

/*
 * Allocate a block in a cylinder group.
 * Note that this routine only allocates fs_bsize blocks; these
 * blocks may be fragmented by the routine that allocates them.
 */
daddr_t
ffs_alloccgblk(const struct inode *ip, struct buf *bp, daddr_t bpref, int flags)
{
	struct fs *fs;
	struct cg *cgp;
	daddr_t bno, blkno;
	u_int8_t *blksfree;
	int cylno, cgbpref;

	fs = ip->i_fs;
	cgp = (struct cg *) bp->b_data;
	blksfree = cg_blksfree(cgp);

	if (bpref == 0) {
		bpref = cgp->cg_rotor;
	} else if ((cgbpref = dtog(fs, bpref)) != cgp->cg_cgx) {
		/* map bpref to correct zone in this cg */
		if (bpref < cgdata(fs, cgbpref))
			bpref = cgmeta(fs, cgp->cg_cgx);
		else
			bpref = cgdata(fs, cgp->cg_cgx);
	}
	/*
	 * If the requested block is available, use it.
	 */
	bno = dtogd(fs, blknum(fs, bpref));
	if (ffs_isblock(fs, blksfree, fragstoblks(fs, bno)))
		goto gotit;
	/*
	 * if the requested data block isn't available and we are trying to
	 * allocate a contiguous file,  return an error.
	 */
	if ((flags & (B_CONTIG | B_METAONLY)) == B_CONTIG)
		return (0);
	/*
	 * Take the next available block in this cylinder group.
	 */
	bno = ffs_mapsearch(fs, cgp, bpref, (int) fs->fs_frag);
	if (bno < 0)
		return (0);

	/* Update cg_rotor only if allocated from the data zone */
	if (bno >= dtogd(fs, cgdata(fs, cgp->cg_cgx)))
		cgp->cg_rotor = bno;

gotit:
	blkno = fragstoblks(fs, bno);
	ffs_clrblock(fs, blksfree, blkno);
	ffs_clusteracct(fs, cgp, blkno, -1);
	cgp->cg_cs.cs_nbfree--;
	fs->fs_cstotal.cs_nbfree--;
	fs->fs_cs(fs, cgp->cg_cgx).cs_nbfree--;

	if (fs->fs_magic != FS_UFS2_MAGIC) {
		cylno = cbtocylno(fs, bno);
		cg_blks(fs, cgp, cylno)[cbtorpos(fs, bno)]--;
		cg_blktot(cgp)[cylno]--;
	}

	fs->fs_fmod = 1;
	blkno = cgbase(fs, cgp->cg_cgx) + bno;

	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_blkmapdep(bp, fs, blkno);

	return (blkno);
}

/*
 * Determine whether a cluster can be allocated.
 *
 * We do not currently check for optimal rotational layout if there
 * are multiple choices in the same cylinder group. Instead we just
 * take the first one that we find following bpref.
 */
daddr_t
ffs_clusteralloc(const struct inode *ip, int cg, daddr_t bpref, int len,
    int flags)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	int i, got, run, bno, bit, map;
	u_char *mapp;
	int32_t *lp;

	fs = ip->i_fs;
	if (fs->fs_maxcluster[cg] < len)
		return (0);

	if (!(bp = ffs_cgread(fs, ip->i_devvp, cg)))
		return (0);

	cgp = (struct cg *)bp->b_data;

	/*
	 * Check to see if a cluster of the needed size (or bigger) is
	 * available in this cylinder group.
	 */
	lp = &cg_clustersum(cgp)[len];
	for (i = len; i <= fs->fs_contigsumsize; i++)
		if (*lp++ > 0)
			break;
	if (i > fs->fs_contigsumsize) {
		/*
		 * This is the first time looking for a cluster in this
		 * cylinder group. Update the cluster summary information
		 * to reflect the true maximum sized cluster so that
		 * future cluster allocation requests can avoid reading
		 * the cylinder group map only to find no clusters.
		 */
		lp = &cg_clustersum(cgp)[len - 1];
		for (i = len - 1; i > 0; i--)
			if (*lp-- > 0)
				break;
		fs->fs_maxcluster[cg] = i;
		goto fail;
	}
	/*
	 * Search the cluster map to find a big enough cluster.
	 * We take the first one that we find, even if it is larger
	 * than we need as we prefer to get one close to the previous
	 * block allocation. We do not search before the current
	 * preference point as we do not want to allocate a block
	 * that is allocated before the previous one (as we will
	 * then have to wait for another pass of the elevator
	 * algorithm before it will be read). We prefer to fail and
	 * be recalled to try an allocation in the next cylinder group.
	 */
	if (dtog(fs, bpref) != cg)
		bpref = cgdata(fs, cg);
	else
		bpref = blknum(fs, bpref);
	bpref = fragstoblks(fs, dtogd(fs, bpref));
	mapp = &cg_clustersfree(cgp)[bpref / NBBY];
	map = *mapp++;
	bit = 1 << (bpref % NBBY);
	for (run = 0, got = bpref; got < cgp->cg_nclusterblks; got++) {
		if ((map & bit) == 0) {
			run = 0;
		} else {
			run++;
			if (run == len)
				break;
		}
		if ((got & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	if (got >= cgp->cg_nclusterblks)
		goto fail;
	/*
	 * Allocate the cluster that we have found.
	 */
	cgp->cg_ffs2_time = cgp->cg_time = time_second;

#ifdef DIAGNOSTIC
	for (i = 1; i <= len; i++)
		if (!ffs_isblock(fs, cg_blksfree(cgp), got - run + i))
			panic("ffs_clusteralloc: map mismatch");
#endif
	bno = cgbase(fs, cg) + blkstofrags(fs, got - run + 1);
#ifdef DIAGNOSTIC
	if (dtog(fs, bno) != cg)
		panic("ffs_clusteralloc: allocated out of group");
#endif

	len = blkstofrags(fs, len);
	for (i = 0; i < len; i += fs->fs_frag)
		if (ffs_alloccgblk(ip, bp, bno + i, flags) != bno + i)
			panic("ffs_clusteralloc: lost block");
	bdwrite(bp);
	return (bno);

fail:
	brelse(bp);
	return (0);
}

/* inode allocation routine */
daddr_t
ffs_nodealloccg(const struct inode *ip, int cg, daddr_t ipref, int mode,
    int flags)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	int start, len, loc, map, i;
#ifdef FFS2
	struct buf *ibp = NULL;
	struct ufs2_dinode *dp2;
#endif

	/*
	 * For efficiency, before looking at the bitmaps for free inodes,
	 * check the counters kept in the superblock cylinder group summaries,
	 * and in the cylinder group itself.
	 */
	fs = ip->i_fs;
	UFS_WAPBL_JLOCK_ASSERT(ip->i_ump->um_mountp);
	if (fs->fs_cs(fs, cg).cs_nifree == 0)
		return (0);

	if (!(bp = ffs_cgread(fs, ip->i_devvp, cg)))
		return (0);

	cgp = (struct cg *)bp->b_data;
	if (cgp->cg_cs.cs_nifree == 0) {
		brelse(bp);
		return (0);
	}

	/*
	 * We are committed to the allocation from now on, so update the time
	 * on the cylinder group.
	 */
	cgp->cg_ffs2_time = cgp->cg_time = time_second;

	/*
	 * If there was a preferred location for the new inode, try to find it.
	 */
	if (ipref) {
		ipref %= fs->fs_ipg;
		if (isclr(cg_inosused(cgp), ipref))
			goto gotit; /* inode is free, grab it. */
	}

	/*
	 * Otherwise, look for the next available inode, starting at cg_irotor
	 * (the position in the bitmap of the last used inode).
	 */
	start = cgp->cg_irotor / NBBY;
	len = howmany(fs->fs_ipg - cgp->cg_irotor, NBBY);
	loc = skpc(0xff, len, &cg_inosused(cgp)[start]);
	if (loc == 0) {
		/*
		 * If we didn't find a free inode in the upper part of the
		 * bitmap (from cg_irotor to the end), then look at the bottom
		 * part (from 0 to cg_irotor).
		 */
		len = start + 1;
		start = 0;
		loc = skpc(0xff, len, &cg_inosused(cgp)[0]);
		if (loc == 0) {
			/*
			 * If we failed again, then either the bitmap or the
			 * counters kept for the cylinder group are wrong.
			 */
			printf("cg = %d, irotor = %d, fs = %s\n",
			    cg, cgp->cg_irotor, fs->fs_fsmnt);
			panic("ffs_nodealloccg: map corrupted");
			/* NOTREACHED */
		}
	}

	/* skpc() returns the position relative to the end */
	i = start + len - loc;

	/*
	 * Okay, so now in 'i' we have the location in the bitmap of a byte
	 * holding a free inode. Find the corresponding bit and set it,
	 * updating cg_irotor as well, accordingly.
	 */
	map = cg_inosused(cgp)[i];
	ipref = i * NBBY;
	for (i = 1; i < (1 << NBBY); i <<= 1, ipref++) {
		if ((map & i) == 0) {
			cgp->cg_irotor = ipref;
			goto gotit;
		}
	}

	printf("fs = %s\n", fs->fs_fsmnt);
	panic("ffs_nodealloccg: block not in map");
	/* NOTREACHED */

gotit:

	UFS_WAPBL_REGISTER_INODE(ip->i_ump->um_mountp, cg * fs->fs_ipg + ipref,
	    mode);

#ifdef FFS2
	/*
	 * For FFS2, check if all inodes in this cylinder group have been used
	 * at least once. If they haven't, and we are allocating an inode past
	 * the last allocated block of inodes, read in a block and initialize
	 * all inodes in it.
	 */
	if (fs->fs_magic == FS_UFS2_MAGIC &&
	    /* Inode is beyond last initialized block of inodes? */
	    ipref + INOPB(fs) > cgp->cg_initediblk &&
	    /* Has any inode not been used at least once? */
	    cgp->cg_initediblk < cgp->cg_ffs2_niblk) {

                ibp = getblk(ip->i_devvp, fsbtodb(fs,
                    ino_to_fsba(fs, cg * fs->fs_ipg + cgp->cg_initediblk)),
                    (int)fs->fs_bsize, 0, 0);

                memset(ibp->b_data, 0, fs->fs_bsize);
                dp2 = (struct ufs2_dinode *)(ibp->b_data);

		/* Give each inode a positive generation number */
                for (i = 0; i < INOPB(fs); i++) {
                        dp2->di_gen = (arc4random() & INT32_MAX) / 2 + 1;
                        dp2++;
                }

		/* Update the counter of initialized inodes */
                cgp->cg_initediblk += INOPB(fs);
        }
#endif /* FFS2 */

	if (DOINGSOFTDEP(ITOV(ip)))
		softdep_setup_inomapdep(bp, ip, cg * fs->fs_ipg + ipref);

	setbit(cg_inosused(cgp), ipref);

	/* Update the counters we keep on free inodes */
	cgp->cg_cs.cs_nifree--;
	fs->fs_cstotal.cs_nifree--;
	fs->fs_cs(fs, cg).cs_nifree--;
	fs->fs_fmod = 1; /* file system was modified */

	/* Update the counters we keep on allocated directories */
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir++;
		fs->fs_cstotal.cs_ndir++;
		fs->fs_cs(fs, cg).cs_ndir++;
	}

#ifdef FFS2
	if (ibp != NULL)
		bawrite(ibp);
#endif

	bdwrite(bp);

	/* Return the allocated inode number */
	return (cg * fs->fs_ipg + ipref);
}

/*
 * Allocate a block or fragment.
 *
 * The specified block or fragment is removed from the
 * free map, possibly fragmenting a block in the process.
 *
 * This implementation should mirror ffs_blkfree()
 */

int
ffs_blkalloc_ump(struct ufsmount *ump, daddr_t bno, long size)
{
	struct fs *fs = ump->um_fs;
	struct cg *cgp;
	struct buf *bp;
	int32_t fragno, cgbno;
	int i, error, cg, blk, frags, bbase;
	u_int8_t *blksfree;

	KASSERT((u_int)size <= fs->fs_bsize && fragoff(fs, size) == 0 &&
	    fragnum(fs, bno) + numfrags(fs, size) <= fs->fs_frag);
	KASSERT(bno < fs->fs_size);

	cg = dtog(fs, bno);
	error = bread(ump->um_devvp, fsbtodb(fs, cgtod(fs, cg)),
		(int)fs->fs_cgsize, &bp);
	if (error) {
		return error;
	}
	cgp = (struct cg *)bp->b_data;
	if (!cg_chkmagic(cgp)) {
		brelse(bp);
		return EIO;
	}
	cgp->cg_ffs2_time = cgp->cg_time = time_second;
	cgbno = dtogd(fs, bno);
	blksfree = cg_blksfree(cgp);

	if (size == fs->fs_bsize) {
		fragno = fragstoblks(fs, cgbno);
		if (!ffs_isblock(fs, blksfree, fragno)) {
			brelse(bp);
			return EBUSY;
		}
		ffs_clrblock(fs, blksfree, fragno);
		ffs_clusteracct(fs, cgp, fragno, -1);
		cgp->cg_cs.cs_nbfree--;
		fs->fs_cstotal.cs_nbfree--;
		fs->fs_cs(fs, cg).cs_nbfree--;
	} else {
		bbase = cgbno - fragnum(fs, cgbno);

		frags = numfrags(fs, size);
		for (i = 0; i < frags; i++) {
			if (isclr(blksfree, cgbno + i)) {
				brelse(bp);
				return EBUSY;
			}
		}
		/*
		 * if a complete block is being split, account for it
		 */
		fragno = fragstoblks(fs, bbase);
		if (ffs_isblock(fs, blksfree, fragno)) {
			cgp->cg_cs.cs_nffree += fs->fs_frag;
			fs->fs_cstotal.cs_nffree += fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree += fs->fs_frag;
			ffs_clusteracct(fs, cgp, fragno, -1);
			cgp->cg_cs.cs_nbfree--;
			fs->fs_cstotal.cs_nbfree--;
			fs->fs_cs(fs, cg).cs_nbfree--;
		}
		/*
		 * decrement the counts associated with the old frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		/*
		 * allocate the fragment
		 */
		for (i = 0; i < frags; i++) {
			clrbit(blksfree, cgbno + i);
		}
		cgp->cg_cs.cs_nffree -= i;
		fs->fs_cstotal.cs_nffree -= i;
		fs->fs_cs(fs, cg).cs_nffree -= i;
		/*
		 * add back in counts associated with the new frags
		 */
		blk = blkmap(fs, blksfree, bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, 1);
	}
	fs->fs_fmod = 1;
	bdwrite(bp);
	return 0;
}

/* XXX pedro: merge this with ffs_blkfree() at some point */
void
ffs_wapbl_blkfree(struct fs *fs, struct vnode *devvp, daddr_t bno, long size)
{
	struct cg *cgp;
	struct buf *bp;
	daddr_t blkno;
	int i, cg, blk, frags, bbase;

	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0 ||
	    fragnum(fs, bno) + numfrags(fs, size) > fs->fs_frag) {
		printf("bsize = %d, size = %ld, fs = %s\n",
		    fs->fs_bsize, size, fs->fs_fsmnt);
		panic("ffs_wapbl_blkfree: bad size");
	}
	cg = dtog(fs, bno);
	if ((u_int)bno >= fs->fs_size) {
		printf("bad block %lld\n", bno);
		ffs_fserr(fs, 0, "bad block");
		return;
	}
	if (!(bp = ffs_cgread(fs, devvp, cg)))
		return;

	cgp = (struct cg *)bp->b_data;
	cgp->cg_ffs2_time = cgp->cg_time = time_second;

	bno = dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		blkno = fragstoblks(fs, bno);
		if (!ffs_isfreeblock(fs, cg_blksfree(cgp), blkno)) {
			printf("block = %lld, fs = %s\n", bno, fs->fs_fsmnt);
			panic("ffs_wapbl_blkfree: freeing free block");
		}
		ffs_setblock(fs, cg_blksfree(cgp), blkno);
		ffs_clusteracct(fs, cgp, blkno, 1);
		cgp->cg_cs.cs_nbfree++;
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;

		if (fs->fs_magic != FS_UFS2_MAGIC) {
			i = cbtocylno(fs, bno);
			cg_blks(fs, cgp, i)[cbtorpos(fs, bno)]++;
			cg_blktot(cgp)[i]++;
		}

	} else {
		bbase = bno - fragnum(fs, bno);
		/*
		 * decrement the counts associated with the old frags
		 */
		blk = blkmap(fs, cg_blksfree(cgp), bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		/*
		 * deallocate the fragment
		 */
		frags = numfrags(fs, size);
		for (i = 0; i < frags; i++) {
			if (isset(cg_blksfree(cgp), bno + i)) {
				printf("block = %lld, fs = %s\n", bno + i,
				    fs->fs_fsmnt);
				panic("ffs_wapbl_blkfree: freeing free frag");
			}
			setbit(cg_blksfree(cgp), bno + i);
		}
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		/*
		 * add back in counts associated with the new frags
		 */
		blk = blkmap(fs, cg_blksfree(cgp), bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, 1);
		/*
		 * if a complete block has been reassembled, account for it
		 */
		blkno = fragstoblks(fs, bbase);
		if (ffs_isblock(fs, cg_blksfree(cgp), blkno)) {
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			ffs_clusteracct(fs, cgp, blkno, 1);
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;

			if (fs->fs_magic != FS_UFS2_MAGIC) {
				i = cbtocylno(fs, bbase);
				cg_blks(fs, cgp, i)[cbtorpos(fs, bbase)]++;
				cg_blktot(cgp)[i]++;
			}
		}
	}
	fs->fs_fmod = 1;
	bdwrite(bp);
}

/*
 * Free a block or fragment.
 *
 * The specified block or fragment is placed back in the
 * free map. If a fragment is deallocated, a possible
 * block reassembly is checked.
 */
void
ffs_blkfree(struct inode *ip, daddr_t bno, long size)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	daddr_t blkno;
	int i, cg, blk, frags, bbase;

	fs = ip->i_fs;
	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0 ||
	    fragnum(fs, bno) + numfrags(fs, size) > fs->fs_frag) {
		printf("dev = 0x%x, bsize = %d, size = %ld, fs = %s\n",
		    ip->i_dev, fs->fs_bsize, size, fs->fs_fsmnt);
		panic("ffs_blkfree: bad size");
	}
	cg = dtog(fs, bno);
	if ((u_int)bno >= fs->fs_size) {
		printf("bad block %lld, ino %u\n", (long long)bno,
		    ip->i_number);
		ffs_fserr(fs, DIP(ip, uid), "bad block");
		return;
	}
	if (!(bp = ffs_cgread(fs, ip->i_devvp, cg)))
		return;

	cgp = (struct cg *)bp->b_data;
	cgp->cg_ffs2_time = cgp->cg_time = time_second;

	bno = dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		blkno = fragstoblks(fs, bno);
		if (!ffs_isfreeblock(fs, cg_blksfree(cgp), blkno)) {
			printf("dev = 0x%x, block = %lld, fs = %s\n",
			    ip->i_dev, (long long)bno, fs->fs_fsmnt);
			panic("ffs_blkfree: freeing free block");
		}
		ffs_setblock(fs, cg_blksfree(cgp), blkno);
		ffs_clusteracct(fs, cgp, blkno, 1);
		cgp->cg_cs.cs_nbfree++;
		fs->fs_cstotal.cs_nbfree++;
		fs->fs_cs(fs, cg).cs_nbfree++;

		if (fs->fs_magic != FS_UFS2_MAGIC) {
			i = cbtocylno(fs, bno);
			cg_blks(fs, cgp, i)[cbtorpos(fs, bno)]++;
			cg_blktot(cgp)[i]++;
		}

	} else {
		bbase = bno - fragnum(fs, bno);
		/*
		 * decrement the counts associated with the old frags
		 */
		blk = blkmap(fs, cg_blksfree(cgp), bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, -1);
		/*
		 * deallocate the fragment
		 */
		frags = numfrags(fs, size);
		for (i = 0; i < frags; i++) {
			if (isset(cg_blksfree(cgp), bno + i)) {
				printf("dev = 0x%x, block = %lld, fs = %s\n",
				    ip->i_dev, (long long)(bno + i),
				    fs->fs_fsmnt);
				panic("ffs_blkfree: freeing free frag");
			}
			setbit(cg_blksfree(cgp), bno + i);
		}
		cgp->cg_cs.cs_nffree += i;
		fs->fs_cstotal.cs_nffree += i;
		fs->fs_cs(fs, cg).cs_nffree += i;
		/*
		 * add back in counts associated with the new frags
		 */
		blk = blkmap(fs, cg_blksfree(cgp), bbase);
		ffs_fragacct(fs, blk, cgp->cg_frsum, 1);
		/*
		 * if a complete block has been reassembled, account for it
		 */
		blkno = fragstoblks(fs, bbase);
		if (ffs_isblock(fs, cg_blksfree(cgp), blkno)) {
			cgp->cg_cs.cs_nffree -= fs->fs_frag;
			fs->fs_cstotal.cs_nffree -= fs->fs_frag;
			fs->fs_cs(fs, cg).cs_nffree -= fs->fs_frag;
			ffs_clusteracct(fs, cgp, blkno, 1);
			cgp->cg_cs.cs_nbfree++;
			fs->fs_cstotal.cs_nbfree++;
			fs->fs_cs(fs, cg).cs_nbfree++;

			if (fs->fs_magic != FS_UFS2_MAGIC) {
				i = cbtocylno(fs, bbase);
				cg_blks(fs, cgp, i)[cbtorpos(fs, bbase)]++;
				cg_blktot(cgp)[i]++;
			}
		}
	}
	fs->fs_fmod = 1;
	bdwrite(bp);
}

int
ffs_inode_free(const struct inode *pip, ufsino_t ino, mode_t mode)
{
	struct vnode *pvp = ITOV(pip);

	if (DOINGSOFTDEP(pvp)) {
		softdep_freefile(pvp, ino, mode);
		return (0);
	}

	return (ffs_freefile(pip, ino, mode));
}

/*
 * Do the actual free operation.
 * The specified inode is placed back in the free map.
 */
int
ffs_freefile(const struct inode *pip, ufsino_t ino, mode_t mode)
{
	struct fs *fs;
	struct cg *cgp;
	struct buf *bp;
	int cg;

	fs = pip->i_fs;
	if ((u_int)ino >= fs->fs_ipg * fs->fs_ncg)
		panic("ffs_freefile: range: dev = 0x%x, ino = %d, fs = %s",
		    pip->i_dev, ino, fs->fs_fsmnt);

	cg = ino_to_cg(fs, ino);
	if (!(bp = ffs_cgread(fs, pip->i_devvp, cg)))
		return (0);

	cgp = (struct cg *)bp->b_data;
	cgp->cg_ffs2_time = cgp->cg_time = time_second;

	ino %= fs->fs_ipg;
	if (isclr(cg_inosused(cgp), ino)) {
		printf("dev = 0x%x, ino = %u, fs = %s\n",
		    pip->i_dev, ino, fs->fs_fsmnt);
		if (fs->fs_ronly == 0)
			panic("ffs_freefile: freeing free inode");
	}
	clrbit(cg_inosused(cgp), ino);
	UFS_WAPBL_UNREGISTER_INODE(pip->i_ump->um_mountp,
	    ino + cg * fs->fs_ipg, mode);
	if (ino < cgp->cg_irotor)
		cgp->cg_irotor = ino;
	cgp->cg_cs.cs_nifree++;
	fs->fs_cstotal.cs_nifree++;
	fs->fs_cs(fs, cg).cs_nifree++;
	if ((mode & IFMT) == IFDIR) {
		cgp->cg_cs.cs_ndir--;
		fs->fs_cstotal.cs_ndir--;
		fs->fs_cs(fs, cg).cs_ndir--;
	}
	fs->fs_fmod = 1;
	bdwrite(bp);
	return (0);
}

#ifdef DIAGNOSTIC
/*
 * Verify allocation of a block or fragment. Returns true if block or
 * fragment is allocated, false if it is free.
 */
int
ffs_checkblk(const struct inode *ip, daddr_t bno, long size)
{
	const struct fs *fs;
	const struct cg *cgp;
	struct buf *bp;
	int i, frags, free;

	fs = ip->i_fs;
	if ((u_int)size > fs->fs_bsize || fragoff(fs, size) != 0) {
		printf("bsize = %d, size = %ld, fs = %s\n",
		    fs->fs_bsize, size, fs->fs_fsmnt);
		panic("ffs_checkblk: bad size");
	}
	if ((u_int)bno >= fs->fs_size)
		panic("ffs_checkblk: bad block %lld", (long long)bno);

	if (!(bp = ffs_cgread(fs, ip->i_devvp, dtog(fs, bno))))
		return (0);

	cgp = (struct cg *)bp->b_data;
	bno = dtogd(fs, bno);
	if (size == fs->fs_bsize) {
		free = ffs_isblock(fs, cg_blksfree(cgp), fragstoblks(fs, bno));
	} else {
		frags = numfrags(fs, size);
		for (free = 0, i = 0; i < frags; i++)
			if (isset(cg_blksfree(cgp), bno + i))
				free++;
		if (free != 0 && free != frags)
			panic("ffs_checkblk: partially free fragment");
	}
	brelse(bp);
	return (!free);
}
#endif /* DIAGNOSTIC */


/*
 * Find a block of the specified size in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
daddr_t
ffs_mapsearch(const struct fs *fs, struct cg *cgp, daddr_t bpref, int allocsiz)
{
	daddr_t bno;
	int start, len, loc, i;
	int blk, field, subfield, pos;

	/*
	 * find the fragment by searching through the free block
	 * map for an appropriate bit pattern
	 */
	if (bpref)
		start = dtogd(fs, bpref) / NBBY;
	else
		start = cgp->cg_frotor / NBBY;
	len = howmany(fs->fs_fpg, NBBY) - start;
	loc = scanc((u_int)len, (u_char *)&cg_blksfree(cgp)[start],
		(u_char *)fragtbl[fs->fs_frag],
		(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
	if (loc == 0) {
		len = start + 1;
		start = 0;
		loc = scanc((u_int)len, (u_char *)&cg_blksfree(cgp)[0],
			(u_char *)fragtbl[fs->fs_frag],
			(u_char)(1 << (allocsiz - 1 + (fs->fs_frag % NBBY))));
		if (loc == 0) {
			printf("start = %d, len = %d, fs = %s\n",
			    start, len, fs->fs_fsmnt);
			panic("ffs_alloccg: map corrupted");
			/* NOTREACHED */
		}
	}
	bno = (start + len - loc) * NBBY;
	cgp->cg_frotor = bno;
	/*
	 * found the byte in the map
	 * sift through the bits to find the selected frag
	 */
	for (i = bno + NBBY; bno < i; bno += fs->fs_frag) {
		blk = blkmap(fs, cg_blksfree(cgp), bno);
		blk <<= 1;
		field = around[allocsiz];
		subfield = inside[allocsiz];
		for (pos = 0; pos <= fs->fs_frag - allocsiz; pos++) {
			if ((blk & field) == subfield)
				return (bno + pos);
			field <<= 1;
			subfield <<= 1;
		}
	}
	printf("bno = %lld, fs = %s\n", (long long)bno, fs->fs_fsmnt);
	panic("ffs_alloccg: block not in map");
	return (-1);
}

/*
 * Update the cluster map because of an allocation or free.
 *
 * Cnt == 1 means free; cnt == -1 means allocating.
 */
void
ffs_clusteracct(struct fs *fs, const struct cg *cgp, daddr_t blkno, int cnt)
{
	int32_t *sump;
	int32_t *lp;
	u_char *freemapp, *mapp;
	int i, start, end, forw, back, map, bit;

	if (fs->fs_contigsumsize <= 0)
		return;
	freemapp = cg_clustersfree(cgp);
	sump = cg_clustersum(cgp);
	/*
	 * Allocate or clear the actual block.
	 */
	if (cnt > 0)
		setbit(freemapp, blkno);
	else
		clrbit(freemapp, blkno);
	/*
	 * Find the size of the cluster going forward.
	 */
	start = blkno + 1;
	end = start + fs->fs_contigsumsize;
	if (end >= cgp->cg_nclusterblks)
		end = cgp->cg_nclusterblks;
	mapp = &freemapp[start / NBBY];
	map = *mapp++;
	bit = 1 << (start % NBBY);
	for (i = start; i < end; i++) {
		if ((map & bit) == 0)
			break;
		if ((i & (NBBY - 1)) != (NBBY - 1)) {
			bit <<= 1;
		} else {
			map = *mapp++;
			bit = 1;
		}
	}
	forw = i - start;
	/*
	 * Find the size of the cluster going backward.
	 */
	start = blkno - 1;
	end = start - fs->fs_contigsumsize;
	if (end < 0)
		end = -1;
	mapp = &freemapp[start / NBBY];
	map = *mapp--;
	bit = 1 << (start % NBBY);
	for (i = start; i > end; i--) {
		if ((map & bit) == 0)
			break;
		if ((i & (NBBY - 1)) != 0) {
			bit >>= 1;
		} else {
			map = *mapp--;
			bit = 1 << (NBBY - 1);
		}
	}
	back = start - i;
	/*
	 * Account for old cluster and the possibly new forward and
	 * back clusters.
	 */
	i = back + forw + 1;
	if (i > fs->fs_contigsumsize)
		i = fs->fs_contigsumsize;
	sump[i] += cnt;
	if (back > 0)
		sump[back] -= cnt;
	if (forw > 0)
		sump[forw] -= cnt;
	/*
	 * Update cluster summary information.
	 */
	lp = &sump[fs->fs_contigsumsize];
	for (i = fs->fs_contigsumsize; i > 0; i--)
		if (*lp-- > 0)
			break;
	fs->fs_maxcluster[cgp->cg_cgx] = i;
}
