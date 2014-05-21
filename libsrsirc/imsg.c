/* imsg.c - handlers for protocol messages
 * libsrsirc - a lightweight serious IRC lib - (C) 2012-14, Timo Buhrmester
 * See README for contact-, COPYING for license information. */

#if HAVE_CONFIG_H
# include <config.h>
#endif

/* C */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include <libsrsirc/irc_defs.h>
#include "iconn.h"
#include <libsrsirc/irc_util.h>
#include <intlog.h>

#include "imsg.h"


static uint8_t
handle_001(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	ic_freearr(hnd->logonconv[0]);
	hnd->logonconv[0] = ic_clonearr(msg);
	ic_strNcpy(hnd->mynick, (*msg)[2],sizeof hnd->mynick);
	char *tmp;
	if ((tmp = strchr(hnd->mynick, '@')))
		*tmp = '\0';
	if ((tmp = strchr(hnd->mynick, '!')))
		*tmp = '\0';

	ic_strNcpy(hnd->umodes, DEF_UMODES, sizeof hnd->umodes);
	ic_strNcpy(hnd->cmodes, DEF_CMODES, sizeof hnd->cmodes);
	hnd->ver[0] = '\0';
	hnd->service = false;

	return 0;
}

static uint8_t
handle_002(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	ic_freearr(hnd->logonconv[1]);
	hnd->logonconv[1] = ic_clonearr(msg);

	return 0;
}

static uint8_t
handle_003(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	ic_freearr(hnd->logonconv[2]);
	hnd->logonconv[2] = ic_clonearr(msg);

	return 0;
}

static uint8_t
handle_004(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	if (nargs < 7)
		return CANT_PROCEED|PROTO_ERR;

	ic_freearr(hnd->logonconv[3]);
	hnd->logonconv[3] = ic_clonearr(msg);
	ic_strNcpy(hnd->myhost, (*msg)[3],sizeof hnd->myhost);
	ic_strNcpy(hnd->umodes, (*msg)[5], sizeof hnd->umodes);
	ic_strNcpy(hnd->cmodes, (*msg)[6], sizeof hnd->cmodes);
	ic_strNcpy(hnd->ver, (*msg)[4], sizeof hnd->ver);
	D("(%p) got beloved 004", hnd);

	return LOGON_COMPLETE;
}

static uint8_t
handle_PING(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	char buf[64];
	snprintf(buf, sizeof buf, "PONG :%s\r\n", (*msg)[2]);
	return icon_write(hnd->con, buf) ? 0 : IO_ERR;
}

static uint8_t
handle_XXX(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	if (!hnd->cb_mut_nick) {
		W("(%p) got no mutnick.. (failing)", hnd);
		return CANT_PROCEED|OUT_OF_NICKS;
	}

	hnd->cb_mut_nick(hnd->mynick, 10); //XXX hc

	if (!hnd->mynick[0]) {
		W("out of nicks");
		return CANT_PROCEED|OUT_OF_NICKS;
	}

	char buf[MAX_NICK_LEN];
	snprintf(buf, sizeof buf, "NICK %s\r\n", hnd->mynick);
	return icon_write(hnd->con, buf) ? 0 : IO_ERR;
}

static uint8_t
handle_432(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static uint8_t
handle_433(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static uint8_t
handle_436(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static uint8_t
handle_437(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static uint8_t
handle_464(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	W("(%p) wrong server password", hnd);
	return CANT_PROCEED|AUTH_ERR;
}

static uint8_t
handle_383(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	ic_strNcpy(hnd->mynick, (*msg)[2],sizeof hnd->mynick);
	char *tmp;
	if ((tmp = strchr(hnd->mynick, '@')))
		*tmp = '\0';
	if ((tmp = strchr(hnd->mynick, '!')))
		*tmp = '\0';

	ic_strNcpy(hnd->myhost, (*msg)[0] ? (*msg)[0] : hnd->con->host,
	    sizeof hnd->myhost);

	ic_strNcpy(hnd->umodes, DEF_UMODES, sizeof hnd->umodes);
	ic_strNcpy(hnd->cmodes, DEF_CMODES, sizeof hnd->cmodes);
	hnd->ver[0] = '\0';
	hnd->service = true;

	return LOGON_COMPLETE;
}

static uint8_t
handle_484(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	hnd->restricted = true;

	return 0;
}

static uint8_t
handle_465(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	W("(%p) we're banned", hnd);
	hnd->banned = true;
	free(hnd->banmsg);
	hnd->banmsg = strdup((*msg)[3] ? (*msg)[3] : "");
	if (!hnd->banned)
		EE("strdup");

	return 0; /* well if we are, the server will sure disconnect ua s*/
}

static uint8_t
handle_466(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	W("(%p) we will be banned", hnd);

	return 0; /* so what, bitch? */
}

static uint8_t
handle_ERROR(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	free(hnd->lasterr);
	hnd->lasterr = strdup((*msg)[2] ? (*msg)[2] : "");
	if (!hnd->lasterr)
		EE("strdup");
	return 0; /* not strictly a case for CANT_PROCEED */
}

static uint8_t
handle_NICK(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	char nick[128];
	if (!pfx_extract_nick(nick, sizeof nick, (*msg)[0]))
		return CANT_PROCEED|PROTO_ERR;

	if (!istrcasecmp(nick, hnd->mynick, hnd->casemapping))
		ic_strNcpy(hnd->mynick, (*msg)[2], sizeof hnd->mynick);
	
	return 0;
}

static uint8_t
handle_005_CASEMAPPING(irc hnd, const char *val)
{
	if (strcasecmp(val, "ascii") == 0)
		hnd->casemapping = CMAP_ASCII;
	else if (strcasecmp(val, "strict-rfc1459") == 0)
		hnd->casemapping = CMAP_STRICT_RFC1459;
	else {
		if (strcasecmp(val, "rfc1459") != 0)
			W("unknown 005 casemapping: '%s'", val);
		hnd->casemapping = CMAP_RFC1459;
	}

	return 0;
}

/* XXX not robust enough */
static uint8_t
handle_005_PREFIX(irc hnd, const char *val)
{
	char str[32];
	if (!val[0])
		return CANT_PROCEED|PROTO_ERR;

	ic_strNcpy(str, val + 1, sizeof str);
	char *p = strchr(str, ')');
	if (!p)
		return CANT_PROCEED|PROTO_ERR;

	*p++ = '\0';

	size_t slen = strlen(str);
	if (slen == 0 || slen != strlen(p))
		return CANT_PROCEED|PROTO_ERR;

	ic_strNcpy(hnd->m005modepfx[0], str, sizeof hnd->m005modepfx[0]);
	ic_strNcpy(hnd->m005modepfx[1], p, sizeof hnd->m005modepfx[1]);

	return 0;
}

static uint8_t
handle_005_CHANMODES(irc hnd, const char *val)
{
	for (int z = 0; z < 4; ++z)
		hnd->m005chanmodes[z][0] = '\0';

	int c = 0;
	char argbuf[64];
	ic_strNcpy(argbuf, val, sizeof argbuf);
	char *ptr = strtok(argbuf, ",");

	while (ptr) {
		if (c < 4)
			ic_strNcpy(hnd->m005chanmodes[c++], ptr,
			    sizeof hnd->m005chanmodes[0]);
		ptr = strtok(NULL, ",");
	}

	if (c != 4)
		W("005 chanmodes: expected 4 params, got %i. arg: \"%s\"",
		    c, val);

	return 0;
}

static uint8_t
handle_005(irc hnd, char *(*msg)[MAX_IRCARGS], size_t nargs)
{
	uint8_t ret = 0;

	for (size_t z = 3; z < nargs; ++z) {
		if (strncasecmp((*msg)[z], "CASEMAPPING=", 12) == 0)
			ret |= handle_005_CASEMAPPING(hnd, (*msg)[z] + 12);
		else if (strncasecmp((*msg)[z], "PREFIX=", 7) == 0)
			ret |= handle_005_PREFIX(hnd, (*msg)[z] + 7);
		else if (strncasecmp((*msg)[z], "CHANMODES=", 10) == 0)
			ret |= handle_005_CHANMODES(hnd, (*msg)[z] + 10);

		if (ret & CANT_PROCEED)
			return ret;
	}

	return ret;
}


uint8_t
handle(irc hnd, char *(*msg)[MAX_IRCARGS])
{
	uint8_t retflags = 0;
	size_t ac = countargs(msg);

	if (strcmp((*msg)[1], "001") == 0) 
		retflags |= handle_001(hnd, msg, ac);
	else if (strcmp((*msg)[1], "002") == 0) 
	      retflags |= handle_002(hnd, msg, ac);
	else if (strcmp((*msg)[1], "003") == 0) 
	      retflags |= handle_003(hnd, msg, ac);
	else if (strcmp((*msg)[1], "004") == 0)
	      retflags |= handle_004(hnd, msg, ac);
	else if (strcmp((*msg)[1], "PING") == 0) 
	      retflags |= handle_PING(hnd, msg, ac);
	else if (strcmp((*msg)[1], "432") == 0) //errorneous nick
	      retflags |= handle_432(hnd, msg, ac);
	else if (strcmp((*msg)[1], "433") == 0) //nick in use
	      retflags |= handle_433(hnd, msg, ac);
	else if (strcmp((*msg)[1], "436") == 0) //nick collision
	      retflags |= handle_436(hnd, msg, ac);
	else if (strcmp((*msg)[1], "437") == 0) //unavail resource
	      retflags |= handle_437(hnd, msg, ac);
	else if (strcmp((*msg)[1], "464") == 0) //passwd missmatch
	      retflags |= handle_464(hnd, msg, ac);
	else if (strcmp((*msg)[1], "383") == 0) //we're service
	      retflags |= handle_383(hnd, msg, ac);
	else if (strcmp((*msg)[1], "484") == 0) //restricted
	      retflags |= handle_484(hnd, msg, ac);
	else if (strcmp((*msg)[1], "465") == 0) //banned
	      retflags |= handle_465(hnd, msg, ac);
	else if (strcmp((*msg)[1], "466") == 0) //will be banned
	      retflags |= handle_466(hnd, msg, ac);
	else if (strcmp((*msg)[1], "NICK") == 0) 
	      retflags |= handle_NICK(hnd, msg, ac);
	else if (strcmp((*msg)[1], "ERROR") == 0) 
	      retflags |= handle_ERROR(hnd, msg, ac);
	else if (strcmp((*msg)[1], "005") == 0) 
	      retflags |= handle_005(hnd, msg, ac);

	return retflags;
}