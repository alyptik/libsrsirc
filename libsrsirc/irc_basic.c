/* irc_basic.c - Front-end implementation
 * libsrsirc - a lightweight serious IRC lib - (C) 2012, Timo Buhrmester
 * See README for contact-, COPYING for license information. */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <common.h>
#include <libsrsirc/irc_util.h>
#include "irc_con.h"

#include <intlog.h>

#include <libsrsirc/irc_basic.h>

#define MAX_IRCARGS ((size_t)15)

#define RFC1459 0

#define DEF_PASS ""
#define DEF_NICK "srsirc"
#define DEF_UNAME "bsnsirc"
#define DEF_FNAME "serious business irc"
#define DEF_CONFLAGS 0
#define DEF_SERV_DIST "*"
#define DEF_SERV_TYPE 0
#define DEF_SERV_INFO "serious business irc service"
#define DEF_UMODES "iswo"
#define DEF_CMODES "opsitnml"

#define DEF_CONTO_HARD 120000000ul
#define DEF_CONTO_SOFT 15000000ul

#define MAX_NICK_LEN 64
#define MAX_HOST_LEN 128
#define MAX_UMODES_LEN 64
#define MAX_CMODES_LEN 64
#define MAX_VER_LEN 128

struct ibhnd
{
	char mynick[MAX_NICK_LEN];
	char myhost[MAX_HOST_LEN];
	bool service;
	char umodes[MAX_UMODES_LEN];
	char cmodes[MAX_CMODES_LEN];
	char ver[MAX_VER_LEN];
	char *lasterr;

	/* zero timeout means no timeout */
	unsigned long conto_hard_us;/*connect() timeout per A/AAAA record*/
	unsigned long conto_soft_us;/*overall ircbas_connect() timeout*/

	bool restricted;
	bool banned;
	char *banmsg;

	int casemapping;

	char *pass;
	char *nick;
	char *uname;
	char *fname;
	unsigned conflags;
	bool serv_con;
	char *serv_dist;
	long serv_type;
	char *serv_info;

	char **logonconv[4];
	char m005chanmodes[4][64];
	char m005modepfx[2][32];

	fp_con_read cb_con_read;
	void *tag_con_read;
	fp_mut_nick cb_mut_nick;

	ichnd_t con;
};



static void mutilate_nick(char *nick, size_t nick_sz);
static bool send_logon(ibhnd_t hnd);

static bool onread(ibhnd_t hnd, char **tok, size_t tok_len);
static char** clonearr(char **arr, size_t nelem);
static void freearr(char **arr, size_t nelem);

static bool handle_001(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_002(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_003(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_004(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_PING(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_432(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_433(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_436(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_437(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_XXX(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_464(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_383(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_484(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_465(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_466(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_ERROR(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_005(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_NICK(ibhnd_t hnd, char **msg, size_t nargs);
static bool handle_005_CASEMAPPING(ibhnd_t hnd, const char *val);
static bool handle_005_PREFIX(ibhnd_t hnd, const char *val);
static bool handle_005_CHANMODES(ibhnd_t hnd, const char *val);

bool
ircbas_regcb_mutnick(ibhnd_t hnd, fp_mut_nick cb)
{
	hnd->cb_mut_nick = cb;
	return true;
}

bool
ircbas_regcb_conread(ibhnd_t hnd, fp_con_read cb, void *tag)
{
	hnd->cb_con_read = cb;
	hnd->tag_con_read = tag;
	return true;
}

ibhnd_t
ircbas_init(void)
{
	ichnd_t con;
	ibhnd_t r = NULL;
	int preverrno = errno;
	errno = 0;
	if (!(con = irccon_init()))
		goto ircbas_init_fail;


	if (!(r = malloc(sizeof (*(ibhnd_t)0))))
		goto ircbas_init_fail;

	r->pass = NULL;
	r->nick = NULL;
	r->uname = NULL;
	r->fname = NULL;
	r->serv_dist = NULL;
	r->serv_info = NULL;
	ic_strNcpy(r->m005chanmodes[0], "b", sizeof r->m005chanmodes[0]);
	ic_strNcpy(r->m005chanmodes[1], "k", sizeof r->m005chanmodes[1]);
	ic_strNcpy(r->m005chanmodes[2], "l", sizeof r->m005chanmodes[2]);
	ic_strNcpy(r->m005chanmodes[3], "psitnm", sizeof r->m005chanmodes[3]);
	ic_strNcpy(r->m005modepfx[0], "ov", sizeof r->m005modepfx[0]);
	ic_strNcpy(r->m005modepfx[1], "@+", sizeof r->m005modepfx[1]);

	if (!(r->pass = strdup(DEF_PASS)))
		goto ircbas_init_fail;

	size_t len = strlen(DEF_NICK);
	if (!(r->nick = malloc((len > 9 ? len : 9) + 1)))
		goto ircbas_init_fail;
	strcpy(r->nick, DEF_NICK);

	if ((!(r->uname = strdup(DEF_UNAME)))
	    || (!(r->fname = strdup(DEF_FNAME)))
	    || (!(r->serv_dist = strdup(DEF_SERV_DIST)))
	    || (!(r->serv_info = strdup(DEF_SERV_INFO))))
		goto ircbas_init_fail;

	errno = preverrno;

	r->con = con;
	/* persistent after reset */
	r->mynick[0] = '\0';
	r->myhost[0] = '\0';
	r->service = false;
	r->umodes[0] = '\0';
	r->cmodes[0] = '\0';
	r->ver[0] = '\0';
	r->lasterr = NULL;

	r->casemapping = CMAP_RFC1459;

	r->restricted = false;
	r->banned = false;
	r->banmsg = NULL;

	r->conflags = DEF_CONFLAGS;
	r->serv_con = false;
	r->serv_type = DEF_SERV_TYPE;

	r->cb_con_read = NULL;
	r->tag_con_read = NULL;
	r->cb_mut_nick = mutilate_nick;

	r->conto_soft_us = DEF_CONTO_SOFT;
	r->conto_hard_us = DEF_CONTO_HARD;

	for(int i = 0; i < 4; i++)
		r->logonconv[i] = NULL;


	D("(%p) irc_bas initialized (backend: %p)", r, r->con);
	return r;

ircbas_init_fail:
	EE("failed to initialize irc_basic handle");
	if (r) {
		free(r->pass);
		free(r->nick);
		free(r->uname);
		free(r->fname);
		free(r->serv_dist);
		free(r->serv_info);
		free(r);
	}

	if (con)
		irccon_dispose(con);

	return NULL;
}

bool
ircbas_reset(ibhnd_t hnd)
{
	D("(%p) resetting backend", hnd);
	if (!irccon_reset(hnd->con))
		return false;

	return true;
}

bool
ircbas_dispose(ibhnd_t hnd)
{
	if (!irccon_dispose(hnd->con))
		return false;

	free(hnd->lasterr);
	free(hnd->banmsg);

	free(hnd->pass);
	free(hnd->nick);
	free(hnd->uname);
	free(hnd->fname);
	free(hnd->serv_dist);
	free(hnd->serv_info);

	D("(%p) disposed", hnd);
	free(hnd);

	return true;
}

bool
ircbas_connect(ibhnd_t hnd)
{
	int64_t tsend = hnd->conto_hard_us ?
	    ic_timestamp_us() + hnd->conto_hard_us : 0;

	free(hnd->lasterr);
	hnd->lasterr = NULL;
	free(hnd->banmsg);
	hnd->banmsg = NULL;
	hnd->banned = false;

	for(int i = 0; i < 4; i++) {
		freearr(hnd->logonconv[i], MAX_IRCARGS);
		hnd->logonconv[i] = NULL;
	}

	D("(%p) connecting backend (timeout: %luus (soft), %luus (hard))",
	    hnd, hnd->conto_soft_us, hnd->conto_hard_us);

	if (!irccon_connect(hnd->con, hnd->conto_soft_us,
	    hnd->conto_hard_us)) {
		W("(%p) backend failed to establish connection", hnd);
		return false;
	}

	D("(%p) sending IRC logon sequence", hnd);
	if (!send_logon(hnd)) {
		W("(%p) failed writing IRC logon sequence", hnd);
		ircbas_reset(hnd);
		return false;
	}

	D("(%p) connection established, IRC logon sequence sent", hnd);
	char *msg[MAX_IRCARGS];
	ic_strNcpy(hnd->mynick, hnd->nick, sizeof hnd->mynick);

	bool success = false;
	int64_t trem = 0;
	do {
		if(tsend) {
			trem = tsend - ic_timestamp_us();
			if (trem <= 0) {
				W("(%p) timeout waiting for 004", hnd);
				ircbas_reset(hnd);
				return false;
			}
		}

		int r = irccon_read(hnd->con, msg, MAX_IRCARGS,
		    (unsigned long)trem);

		if (r < 0) {
			W("(%p) irccon_read() failed", hnd);
			ircbas_reset(hnd);
			return false;
		}

		if (r == 0)
			continue;

		if (hnd->cb_con_read && !hnd->cb_con_read(msg, MAX_IRCARGS,
		    hnd->tag_con_read)) {
			W("(%p) further logon prohibited by conread", hnd);
			ircbas_reset(hnd);
			return false;
		}

		size_t ac = 2;
		while (ac < MAX_IRCARGS && msg[ac])
			ac++;

		bool failure = false;

		/* these are the protocol messages we deal with.
		 * seeing 004 or 383 makes us consider ourselves logged on
		 * note that we do not wait for 005, but we will later
		 * parse it as we ran across it. */
		if (strcmp(msg[1], "001") == 0) {
			if (!(handle_001(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "002") == 0) {
			if (!(handle_002(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "003") == 0) {
			if (!(handle_003(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "004") == 0) {
			if (!(handle_004(hnd, msg, ac)))
				failure = true;
			success = true;
		} else if (strcmp(msg[1], "PING") == 0) {
			if (!(handle_PING(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "432") == 0) { //errorneous nick
			if (!(handle_432(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "433") == 0) { //nick in use
			if (!(handle_433(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "436") == 0) { //nick collision
			if (!(handle_436(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "437") == 0) { //unavail resource
			if (!(handle_437(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "464") == 0) { //passwd missmatch
			handle_464(hnd, msg, ac);
			failure = true;
		} else if (strcmp(msg[1], "383") == 0) { //we're service
			if (!(handle_383(hnd, msg, ac)))
				failure = true;
			success = true;
		} else if (strcmp(msg[1], "484") == 0) { //restricted
			if (!(handle_484(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "465") == 0) { //banned
			if (!(handle_465(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "466") == 0) { //will be banned
			if (!(handle_466(hnd, msg, ac)))
				failure = true;
		} else if (strcmp(msg[1], "ERROR") == 0) {
			handle_ERROR(hnd, msg, ac);
			W("(%p) received error while logging on: %s",
			    hnd, msg[2]);
			failure = true;
		}

		if (failure) {
			char line[1024];
			sndumpmsg(line, sizeof line, NULL, msg, MAX_IRCARGS);
			E("choked on '%s' (colontrail: %d",
			    line, irccon_colon_trail(hnd->con));
			ircbas_reset(hnd);
			return false;
		}

	} while (!success);
	D("(%p) irc logon finished, U R online", hnd);
	return true;
}



int
ircbas_read(ibhnd_t hnd, char **tok, size_t tok_len, unsigned long to_us)
{
	//D("(%p) wanna read (timeout: %lu)", hnd, to_us);
	int r = irccon_read(hnd->con, tok, tok_len, to_us);

	if (r == -1 || (r != 0 && !onread(hnd, tok, tok_len))) {
		W("(%p) irccon_read() failed or onread() denied (r:%d)",
		    hnd, r);
		ircbas_reset(hnd);
		return -1;
	}
	//D("(%p) done reading", hnd);

	return r;
}

bool
ircbas_write(ibhnd_t hnd, const char *line)
{
	bool r = irccon_write(hnd->con, line);

	if (!r) {
		W("(%p) irccon_write() failed", hnd);
		ircbas_reset(hnd);
	}

	return r;
}

bool
ircbas_online(ibhnd_t hnd)
{
	return irccon_online(hnd->con);
}
bool
ircbas_set_ssl(ibhnd_t hnd, bool on)
{
	return irccon_set_ssl(hnd->con, on);
}

bool
ircbas_get_ssl(ibhnd_t hnd)
{
	return irccon_get_ssl(hnd->con);
}

int
ircbas_sockfd(ibhnd_t hnd)
{
	return irccon_sockfd(hnd->con);
}

bool
ircbas_set_pass(ibhnd_t hnd, const char *srvpass)
{
	char *n = strdup(srvpass);
	if (!n)
		EE("strdup");
	else {
		free(hnd->pass);
		hnd->pass = n;
	}
	return n;
}

bool
ircbas_set_uname(ibhnd_t hnd, const char *uname)
{
	char *n = strdup(uname);
	if (!n)
		EE("strdup");
	else {
		free(hnd->uname);
		hnd->uname = n;
	}
	return n;
}

bool
ircbas_set_fname(ibhnd_t hnd, const char *fname)
{
	char *n = strdup(fname);
	if (!n)
		EE("strdup");
	else {
		free(hnd->fname);
		hnd->fname = n;
	}
	return n;
}

bool
ircbas_set_conflags(ibhnd_t hnd, unsigned flags)
{
	hnd->conflags = flags;
	return true;
}

bool
ircbas_set_nick(ibhnd_t hnd, const char *nick)
{
	char *n = strdup(nick);
	if (!n)
		EE("strdup");
	else {
		free(hnd->nick);
		hnd->nick = n;
	}
	return n;
}

bool
ircbas_set_service_connect(ibhnd_t hnd, bool enabled)
{
	hnd->serv_con = enabled;
	return true;
}

bool
ircbas_set_service_dist(ibhnd_t hnd, const char *dist)
{
	char *n = strdup(dist);
	if (!n)
		EE("strdup");
	else {
		free(hnd->serv_dist);
		hnd->serv_dist = n;
	}
	return n;
}

bool
ircbas_set_service_type(ibhnd_t hnd, long type)
{
	hnd->serv_type = type;
	return true;
}

bool
ircbas_set_service_info(ibhnd_t hnd, const char *info)
{
	char *n = strdup(info);
	if (!n)
		EE("strdup");
	else {
		free(hnd->serv_info);
		hnd->serv_info = n;
	}
	return n;
}

bool
ircbas_set_connect_timeout(ibhnd_t hnd,
    unsigned long soft, unsigned long hard)
{
	hnd->conto_hard_us = hard;
	hnd->conto_soft_us = soft;
	return true;
}

bool
ircbas_set_proxy(ibhnd_t hnd, const char *host, unsigned short port,
    int ptype)
{
	return irccon_set_proxy(hnd->con, host, port, ptype);
}

bool
ircbas_set_server(ibhnd_t hnd, const char *host, unsigned short port)
{
	return irccon_set_server(hnd->con, host, port);
}

int
ircbas_casemap(ibhnd_t hnd)
{
	return hnd->casemapping;
}

const char*
ircbas_mynick(ibhnd_t hnd)
{
	return hnd->mynick;
}

const char*
ircbas_myhost(ibhnd_t hnd)
{
	return hnd->myhost;
}

bool
ircbas_service(ibhnd_t hnd)
{
	return hnd->service;
}

const char*
ircbas_umodes(ibhnd_t hnd)
{
	return hnd->umodes;
}

const char*
ircbas_cmodes(ibhnd_t hnd)
{
	return hnd->cmodes;
}

const char*
ircbas_version(ibhnd_t hnd)
{
	return hnd->ver;
}

const char*
ircbas_lasterror(ibhnd_t hnd)
{
	return hnd->lasterr;
}
const char*
ircbas_banmsg(ibhnd_t hnd)
{
	return hnd->banmsg;
}
bool
ircbas_banned(ibhnd_t hnd)
{
	return hnd->banned;
}
bool
ircbas_colon_trail(ibhnd_t hnd)
{
	return irccon_colon_trail(hnd->con);
}

const char*
ircbas_get_proxy_host(ibhnd_t hnd)
{
	return irccon_get_proxy_host(hnd->con);
}

unsigned short
ircbas_get_proxy_port(ibhnd_t hnd)
{
	return irccon_get_proxy_port(hnd->con);
}

int
ircbas_get_proxy_type(ibhnd_t hnd)
{
	return irccon_get_proxy_type(hnd->con);
}

const char*
ircbas_get_host(ibhnd_t hnd)
{
	return irccon_get_host(hnd->con);
}

unsigned short
ircbas_get_port(ibhnd_t hnd)
{
	return irccon_get_port(hnd->con);
}

const char*
ircbas_get_pass(ibhnd_t hnd)
{
	return hnd->pass;
}

const char*
ircbas_get_uname(ibhnd_t hnd)
{
	return hnd->uname;
}

const char*
ircbas_get_fname(ibhnd_t hnd)
{
	return hnd->fname;
}

unsigned
ircbas_get_conflags(ibhnd_t hnd)
{
	return hnd->conflags;
}

const char*
ircbas_get_nick(ibhnd_t hnd)
{
	return hnd->nick;
}

bool
ircbas_get_service_connect(ibhnd_t hnd)
{
	return hnd->serv_con;
}

const char*
ircbas_get_service_dist(ibhnd_t hnd)
{
	return hnd->serv_dist;
}

long
ircbas_get_service_type(ibhnd_t hnd)
{
	return hnd->serv_type;
}

const char*
ircbas_get_service_info(ibhnd_t hnd)
{
	return hnd->serv_info;
}

static void
mutilate_nick(char *nick, size_t nick_sz)
{if (nick_sz){} //XXX
	size_t len = strlen(nick);
	if (len < 9) {
		nick[len++] = '_';
		nick[len] = '\0';
	} else {
		char last = nick[len-1];
		if (last == '9')
			nick[1 + rand() % (len-1)] = '0' + rand() % 10;
		else if ('0' <= last && last <= '8')
			nick[len - 1] = last + 1;
		else
			nick[len - 1] = '0';
	}
}

static bool
send_logon(ibhnd_t hnd)
{
	if (!irccon_online(hnd->con))
		return false;
	char aBuf[256];
	char *pBuf = aBuf;
	aBuf[0] = '\0';
	int rem = (int)sizeof aBuf;
	int r;

	if (hnd->pass && strlen(hnd->pass) > 0) {
		r = snprintf(pBuf, rem, "PASS :%s\r\n", hnd->pass);
		rem -= r;
		pBuf += r;
	}

	if (rem <= 0)
		return false;

	if (hnd->service) {
		r = snprintf(pBuf, rem, "SERVICE %s 0 %s %ld 0 :%s\r\n",
		    hnd->nick, hnd->serv_dist, hnd->serv_type,
		    hnd->serv_info);
	} else {
		r = snprintf(pBuf, rem, "NICK %s\r\nUSER %s %u * :%s\r\n",
		    hnd->nick, hnd->uname, hnd->conflags, hnd->fname);
	}

	rem -= r;
	pBuf += r;
	if (rem < 0)
		return false;

	return ircbas_write(hnd, aBuf);
}

static size_t
countargs(char **tok, size_t tok_len)
{
	size_t ac = 2;
	while (ac < tok_len && tok[ac])
		ac++;
	return ac;
}

static bool
onread(ibhnd_t hnd, char **tok, size_t tok_len)
{
	bool failure = false;

	if (strcmp(tok[1], "NICK") == 0) {
		if (!handle_NICK(hnd, tok, countargs(tok, tok_len)))
			failure = true;
	} else if (strcmp(tok[1], "ERROR") == 0) {
		if (!handle_ERROR(hnd, tok, countargs(tok, tok_len)))
			failure = true;
	} else if (strcmp(tok[1], "005") == 0) {
		if (!handle_005(hnd, tok, countargs(tok, tok_len)))
			failure = true;
	}

	if (failure) {
		char line[1024];
		sndumpmsg(line, sizeof line, NULL, tok, tok_len);
		E("choked on '%s' (colontrail: %d",
		    line, irccon_colon_trail(hnd->con));
	}

	return !failure;
}

const char *const *const*
ircbas_logonconv(ibhnd_t hnd)
{
	return (const char *const *const*)hnd->logonconv;
}

const char *const*
ircbas_005chanmodes(ibhnd_t hnd)
{
	return (const char *const*)hnd->m005chanmodes;
}

const char *const*
ircbas_005modepfx(ibhnd_t hnd)
{
	return (const char *const*)hnd->m005modepfx;
}


static char**
clonearr(char **arr, size_t nelem)
{
	char **res = malloc((nelem+1) * sizeof *arr);
	if (!res) {
		EE("malloc");
		return NULL;
	}

	for(size_t i = 0; i < nelem; i++) {
		if (arr[i]) {
			if (!(res[i] = strdup(arr[i]))) {
				EE("strdup");
				goto clonearr_fail;
			}
		} else
			res[i] = NULL;
	}
	res[nelem] = NULL;
	return res;

clonearr_fail:

	freearr(res, nelem);
	return NULL;
}


static void
freearr(char **arr, size_t nelem)
{
	if (arr) {
		for(size_t i = 0; i < nelem; i++)
			free(arr[i]);
		free(arr);
	}
}
static bool
handle_001(ibhnd_t hnd, char **msg, size_t nargs)
{
	hnd->logonconv[0] = clonearr(msg, MAX_IRCARGS);
	ic_strNcpy(hnd->mynick, msg[2],sizeof hnd->mynick);
	char *tmp;
	if ((tmp = strchr(hnd->mynick, '@')))
		*tmp = '\0';
	if ((tmp = strchr(hnd->mynick, '!')))
		*tmp = '\0';

	ic_strNcpy(hnd->myhost, msg[0] ?
	    msg[0] : irccon_get_host(hnd->con), sizeof hnd->mynick);

	ic_strNcpy(hnd->umodes, DEF_UMODES, sizeof hnd->umodes);
	ic_strNcpy(hnd->cmodes, DEF_CMODES, sizeof hnd->cmodes);
	hnd->ver[0] = '\0';
	hnd->service = false;

	return true;
}

static bool
handle_002(ibhnd_t hnd, char **msg, size_t nargs)
{
	hnd->logonconv[1] = clonearr(msg, MAX_IRCARGS);

	return true;
}

static bool
handle_003(ibhnd_t hnd, char **msg, size_t nargs)
{
	hnd->logonconv[2] = clonearr(msg, MAX_IRCARGS);

	return true;
}

static bool
handle_004(ibhnd_t hnd, char **msg, size_t nargs)
{
	hnd->logonconv[3] = clonearr(msg, MAX_IRCARGS);
	ic_strNcpy(hnd->myhost, msg[3],sizeof hnd->myhost);
	ic_strNcpy(hnd->umodes, msg[5], sizeof hnd->umodes);
	ic_strNcpy(hnd->cmodes, msg[6], sizeof hnd->cmodes);
	ic_strNcpy(hnd->ver, msg[4], sizeof hnd->ver);
	D("(%p) got beloved 004", hnd);

	return true;
}

static bool
handle_PING(ibhnd_t hnd, char **msg, size_t nargs)
{
	char buf[64];
	snprintf(buf, sizeof buf, "PONG :%s\r\n", msg[2]);
	return irccon_write(hnd->con, buf);
}

static bool
handle_432(ibhnd_t hnd, char **msg, size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static bool
handle_433(ibhnd_t hnd, char **msg, size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static bool
handle_436(ibhnd_t hnd, char **msg, size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static bool
handle_437(ibhnd_t hnd, char **msg, size_t nargs)
{
	return handle_XXX(hnd, msg, nargs);
}

static bool
handle_XXX(ibhnd_t hnd, char **msg, size_t nargs)
{
	if (!hnd->cb_mut_nick) {
		W("(%p) got no mutnick.. (failing)", hnd);
		return false;
	}

	hnd->cb_mut_nick(hnd->mynick, 10); //XXX hc
	char buf[MAX_NICK_LEN];
	snprintf(buf,sizeof buf,"NICK %s\r\n",hnd->mynick);
	return irccon_write(hnd->con, buf);
}

static bool
handle_464(ibhnd_t hnd, char **msg, size_t nargs)
{
	W("(%p) wrong server password", hnd);
	return true;
}

static bool
handle_383(ibhnd_t hnd, char **msg, size_t nargs)
{
	ic_strNcpy(hnd->mynick, msg[2],sizeof hnd->mynick);
	char *tmp;
	if ((tmp = strchr(hnd->mynick, '@')))
		*tmp = '\0';
	if ((tmp = strchr(hnd->mynick, '!')))
		*tmp = '\0';

	ic_strNcpy(hnd->myhost,
	    msg[0] ? msg[0] : irccon_get_host(hnd->con),
	    sizeof hnd->mynick);

	ic_strNcpy(hnd->umodes, DEF_UMODES, sizeof hnd->umodes);
	ic_strNcpy(hnd->cmodes, DEF_CMODES, sizeof hnd->cmodes);
	hnd->ver[0] = '\0';
	hnd->service = true;

	return true;
}

static bool
handle_484(ibhnd_t hnd, char **msg, size_t nargs)
{
	hnd->restricted = true;

	return true;
}

static bool
handle_465(ibhnd_t hnd, char **msg, size_t nargs)
{
	W("(%p) we're banned", hnd);
	hnd->banned = true;
	free(hnd->banmsg);
	hnd->banmsg = strdup(msg[3] ? msg[3] : "");
	if (!hnd->banned)
		EE("strdup");

	return true;
}

static bool
handle_466(ibhnd_t hnd, char **msg, size_t nargs)
{
	W("(%p) we will be banned", hnd);

	return true;
}

static bool
handle_ERROR(ibhnd_t hnd, char **msg, size_t nargs)
{
	free(hnd->lasterr);
	hnd->lasterr = strdup(msg[2] ? msg[2] : "");
	if (!hnd->lasterr)
		EE("strdup");
	return true;
}

static bool
handle_NICK(ibhnd_t hnd, char **msg, size_t nargs)
{
	char nick[128];
	if (!pfx_extract_nick(nick, sizeof nick, msg[0]))
		return false;

	if (!istrcasecmp(nick, hnd->mynick, hnd->casemapping))
		ic_strNcpy(hnd->mynick, msg[2], sizeof hnd->mynick);
	
	return true;
}
static bool
handle_005_CASEMAPPING(ibhnd_t hnd, const char *val)
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

	return true;
}

/* XXX not robust enough */
static bool
handle_005_PREFIX(ibhnd_t hnd, const char *val)
{
	char str[32];
	ic_strNcpy(str, val + 1, sizeof str);
	char *p = strchr(str, ')');
	*p++ = '\0';
	ic_strNcpy(hnd->m005modepfx[0], str, sizeof hnd->m005modepfx[0]);
	ic_strNcpy(hnd->m005modepfx[1], p, sizeof hnd->m005modepfx[1]);

	return true;
}

static bool
handle_005_CHANMODES(ibhnd_t hnd, const char *val)
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

	return true;
}

static bool
handle_005(ibhnd_t hnd, char **msg, size_t nargs)
{
	bool failure = false;

	for (size_t z = 3; z < nargs; ++z) {
		if (strncasecmp(msg[z], "CASEMAPPING=", 12) == 0) {
			if (!(handle_005_CASEMAPPING(hnd, msg[z] + 12)))
				failure = true;
		} else if (strncasecmp(msg[z], "PREFIX=", 7) == 0) {
			if (!(handle_005_PREFIX(hnd, msg[z] + 7)))
				failure = true;
		} else if (strncasecmp(msg[z], "CHANMODES=", 10) == 0) {
			if (!(handle_005_CHANMODES(hnd, msg[z] + 10)))
				failure = true;
		}

		if (failure)
			return false;
	}

	return true;
}
