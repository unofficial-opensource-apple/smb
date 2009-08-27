/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Portions Copyright (C) 2001 - 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: nb_name.c,v 1.12 2005/05/06 23:16:29 lindak Exp $
 */
#include <sys/param.h>
#include <sys/socket.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netsmb/netbios.h>
#include <netsmb/smb_lib.h>
#include <netsmb/nb_lib.h>
#include <sys/mchain.h>

int
nb_snballoc(int namelen, struct sockaddr_nb **dst)
{
	struct sockaddr_nb *snb;
	int slen;

	slen = namelen + (int)sizeof(*snb) - (int)sizeof(snb->snb_name);
	snb = malloc(slen);
	if (snb == NULL)
		return ENOMEM;
	bzero(snb, slen);
	snb->snb_family = AF_NETBIOS;
	snb->snb_len = slen;
	*dst = snb;
	return 0;
}

/*
 * Create a full NETBIOS address
 */
int
nb_sockaddr(struct sockaddr *peer, struct nb_name *np,
	struct sockaddr_nb **dst)

{
	struct sockaddr_nb *snb;
	int nmlen, error;

	if (peer && (peer->sa_family != AF_INET))
		return EPROTONOSUPPORT;
	nmlen = nb_name_len(np);
	if (nmlen < NB_ENCNAMELEN)
		return EINVAL;
	error = nb_snballoc(nmlen, &snb);
	if (error)
		return error;
	/* the only time the first argument to nb_name_encode()
	*  is not NULL is when we are processing the server's
	*  computer name from NBNS (the name with <20> at the
	*  end), and we DON'T want to uppercase it, because we
	*  don't know what the encoding is */
	u_int8_t	UCflag=1; /* to upper or not to upper?
				     that is the question */
	if (peer)
		UCflag = 0; /* don't do it! */
	/* 
	 * nmlen should already be set to the value return by
	 * nb_name_encode. The old code had a debug print here
	 * if they returned different value. Remove the debug 
	 * code.
	 */ 
	nmlen = nb_name_encode(np, snb->snb_name,UCflag);
	if (peer)
		memcpy(&snb->snb_addrin, peer, peer->sa_len);
	*dst = snb;
	return 0;
}

int
nb_name_len(struct nb_name *np)
{
	u_char *name;
	int len, sclen;

	len = 1 + NB_ENCNAMELEN;
	if (np->nn_scope == NULL)
		return len + 1;
	sclen = 0;
	for (name = np->nn_scope; *name; name++) {
		if (*name == '.') {
			sclen = 0;
		} else {
			if (sclen < NB_MAXLABLEN) {
				sclen++;
				len++;
			}
		}
	}
	return len + 1;
}

int
nb_encname_len(const char *str)
{
	const u_char *cp = (const u_char *)str;
	int len, blen;

	if ((cp[0] & 0xc0) == 0xc0)
		return -1;	/* first two bytes are offset to name */

	len = 1;
	for (;;) {
		blen = *cp;
		if (blen++ == 0)
			break;
		len += blen;
		cp += blen;
	}
	return len;
}

/* B4BP (7/23/01 sent to BP) endian fix! */
#define	NBENCODE(c)	(htoles((u_short)(((u_char)(c) >> 4) | \
			 (((u_char)(c) & 0xf) << 8)) + 0x4141))

static void
memsetw(char *dst, int n, u_short word)
{
	while (n--) {
		*(u_short*)dst = word;
		dst += 2;
	}
}

int
/* add new arg UCflag. 1=uppercase, 0=don't */
nb_name_encode(struct nb_name *np, u_char *dst, u_int8_t UCflag)
{
	u_char *name, *plen;
	u_char *cp = dst;
	int i, lblen;

	*cp++ = NB_ENCNAMELEN;
	name = np->nn_name;
	if (name[0] == '*' && name[1] == 0) {
		*(u_short*)cp = NBENCODE('*');
		memsetw((char *)cp + 2, NB_NAMELEN - 1, NBENCODE((char)0));
		cp += NB_ENCNAMELEN;
	} else {
		/* freebsd bug: system names must be truncated to 15 chars not 16 */
		for (i = 0; *name && i < NB_NAMELEN - 1; i++, cp += 2, name++)

			if (UCflag) /* caller wants uppercase */ {
				*(u_short*)cp = NBENCODE(toupper(*name));
			} else {
				*(u_short*)cp = NBENCODE(*name);
			}

		i = NB_NAMELEN - i - 1;
		if (i > 0) {
			memsetw((char *)cp, i, NBENCODE(' '));
			cp += i * 2;
		}
		*(u_short*)cp = NBENCODE(np->nn_type);
		cp += 2;
	}
	*cp = 0;
	if (np->nn_scope == NULL)
		return nb_encname_len((char *)dst);
	plen = cp++;
	lblen = 0;
	for (name = np->nn_scope; ; name++) {
		if (*name == '.' || *name == 0) {
			*plen = lblen;
			plen = cp++;
			*plen = 0;
			if (*name == 0)
				break;
		} else {
			if (lblen < NB_MAXLABLEN) {
				*cp++ = *name;
				lblen++;
			}
		}
	}
	return nb_encname_len((char *)dst);
}

