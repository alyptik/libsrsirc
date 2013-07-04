/* irc_util.c - Implementation of misc. functions related to IRC
 * libsrsirc - a lightweight serious IRC lib - (C) 2012, Timo Buhrmester
 * See README for contact-, COPYING for license information.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#define _GNU_SOURCE 1

#include <libsrsirc/irc_util.h>

#include <common.h>


#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#include "debug.h"

#define CHANMODE_CLASS_A 1 /*do not change these, see int classify_chanmode(char)*/
#define CHANMODE_CLASS_B 2
#define CHANMODE_CLASS_C 3
#define CHANMODE_CLASS_D 4

static int classify_chanmode(char c, const char *const *chmodes);

int pxtypeno(const char *typestr)
{
	return (strcasecmp(typestr, "socks4") == 0) ? IRCPX_SOCKS4 :
	       (strcasecmp(typestr, "socks5") == 0) ? IRCPX_SOCKS5 :
	       (strcasecmp(typestr, "http") == 0) ? IRCPX_HTTP : -1;
}

const char *pxtypestr(int type)
{
	return (type == IRCPX_HTTP) ? "HTTP" :
	       (type == IRCPX_SOCKS4) ? "SOCKS4" :
	       (type == IRCPX_SOCKS5) ? "SOCKS5" : "unknown";
}

bool pfx_extract_nick(char *dest, size_t dest_sz, const char *pfx)
{
	if (!dest || !dest_sz || !pfx)
		return false;
	strncpy(dest, pfx, dest_sz);
	dest[dest_sz-1] = '\0';

	char *ptr = strchr(dest, '@');
	if (ptr)
		*ptr = '\0';

	ptr = strchr(dest, '!');
	if (ptr)
		*ptr = '\0';

	return true;
}

bool pfx_extract_uname(char *dest, size_t dest_sz, const char *pfx)
{
	if (!dest || !dest_sz || !pfx)
		return false;
	strncpy(dest, pfx, dest_sz);
	dest[dest_sz-1] = '\0';

	char *ptr = strchr(dest, '@');
	if (ptr)
		*ptr = '\0';

	ptr = strchr(dest, '!');
	if (ptr)
		memmove(dest, ptr+1, strlen(ptr+1)+1);
	else
		*dest = '\0';

	return true;
}

bool pfx_extract_host(char *dest, size_t dest_sz, const char *pfx)
{
	if (!dest || !dest_sz || !pfx)
		return false;
	strncpy(dest, pfx, dest_sz);
	dest[dest_sz-1] = '\0';

	char *ptr = strchr(dest, '@');
	if (ptr)
		memmove(dest, ptr+1, strlen(ptr+1)+1);
	else
		*dest = '\0';

	return true;
}

int
istrcasecmp(const char *n1, const char *n2, int casemap)
{
	size_t l1 = strlen(n1);
	size_t l2 = strlen(n2);

	return istrncasecmp(n1, n2,
			(l1 < l2) ? (l1 + 1) : (l2 + 1), casemap);
}

int
istrncasecmp(const char *n1, const char *n2, size_t len, int casemap)
{
	if (len == 0)
		return 0;

	char *d1 = strdup(n1);
	char *d2 = strdup(n2);

	itolower(d1, strlen(d1) + 1, n1, casemap);
	itolower(d2, strlen(d2) + 1, n2, casemap);

	int i = strncmp(d1, d2, len);

	free(d1);
	free(d2);
	return i;
}

void
itolower(char *dest, size_t destsz, const char *str, int casemap)
{
	int rangeinc;
	switch (casemap)
	{
	case CASEMAPPING_RFC1459:
		rangeinc = 4;
		break;
	case CASEMAPPING_STRICT_RFC1459:
		rangeinc = 3;
		break;
	default:
		rangeinc = 0;
	}

	size_t c = 0;
	char *ptr = dest;
	while(c < destsz) {
		if (*str >= 'A' && *str <= ('Z'+rangeinc))
			*ptr++ = *str + ('a'-'A');
		else
			*ptr++ = *str;
		
		if (!*str)
			break;
		str++;
	}

	dest[destsz-1] = '\0';
}

bool
parse_pxspec(char *pxtypestr, size_t pxtypestr_sz, char *hoststr,
		size_t hoststr_sz, unsigned short *port, const char *line)
{
	char linebuf[128];
	strncpy(linebuf, line, sizeof linebuf);
	linebuf[sizeof linebuf - 1] = '\0';

	char *ptr = strchr(linebuf, ':');
	if (!ptr)
		return false;
	
	size_t num = (size_t)(ptr - linebuf) < pxtypestr_sz
			? (size_t)(ptr - linebuf)
			: pxtypestr_sz - 1;

	strncpy(pxtypestr, linebuf, num);
	pxtypestr[num] = '\0';

	parse_hostspec(hoststr, hoststr_sz, port, ptr + 1);
	return true;

}

void parse_hostspec(char *hoststr, size_t hoststr_sz, unsigned short *port,
		const char *line)
{
	strncpy(hoststr, line, hoststr_sz);
	char *ptr = strchr(hoststr, ']');
	if (!ptr)
		ptr = hoststr;
	ptr = strchr(ptr, ':');
	if (ptr) {
		*port = (unsigned short)strtol(ptr+1, NULL, 10);
		*ptr = '\0';
	} else
		*port = 0;
}

bool parse_identity(char *nick, size_t nicksz, char *uname, size_t unamesz,
		char *fname, size_t fnamesz, const char *identity)
{
	char ident[256];
	ic_strNcpy(ident, identity, sizeof ident);

	char *p = strchr(ident, ' ');
	if (p)
		ic_strNcpy(fname, (*p = '\0', p + 1), fnamesz);
	else
		return false;
	p = strchr(ident, '!');
	if (p)
		ic_strNcpy(uname, (*p = '\0', p + 1), unamesz);
	else
		return false;

	ic_strNcpy(nick, ident, nicksz);
	return true;
}



void
sndumpmsg(char *dest, size_t dest_sz, void *tag, char **msg, size_t msg_len)
{
	snprintf(dest, dest_sz, "(%p) '%s' '%s'", tag, msg[0], msg[1]);
	for(size_t i = 2; i < msg_len; i++) {
		if (!msg[i])
			break;
		ic_strNcat(dest, " '", dest_sz);
		ic_strNcat(dest, msg[i], dest_sz);
		ic_strNcat(dest, "'", dest_sz);
	}
}

void
dumpmsg(void *tag, char **msg, size_t msg_len)
{
	char buf[1024];
	sndumpmsg(buf, sizeof buf, tag, msg, msg_len);
	fprintf(stderr, "%s\n", buf);
}


bool
cr(char **msg, size_t msg_len, void *tag)
{
	dumpmsg(tag, msg, msg_len);
	return true;
}

char**
parse_chanmodes(const char *const *arr, size_t argcount, size_t *num, const char *modepfx005chr, const char *const *chmodes)
{
	char *modes = strdup(arr[0]);
	const char *arg;
	size_t nummodes = strlen(modes) - (ic_strCchr(modes,'-') + ic_strCchr(modes,'+'));
	char **modearr = malloc(nummodes * sizeof *modearr);
	size_t i = 1;
	int j = 0, cl;
	char *ptr = modes;
	int enable = 1;
	WVX("modes: '%s', nummodes: %zu, modepfx005chr: '%s'", modes, nummodes, modepfx005chr);
	while (*ptr) {
		char c = *ptr;
		WVX("next modechar is '%c', enable ATM: %d", c, enable);
		arg = NULL;
		switch (c) {
		case '+':
			enable = 1;
			ptr++;
			continue;
		case '-':
			enable = 0;
			ptr++;
			continue;
		default:
			cl = classify_chanmode(c, chmodes);
			WVX("classified mode '%c' to class %d", c, cl);
			switch (cl) {
			case CHANMODE_CLASS_A:
				/*i>=argcount: this may happen when requesting modes outside of that channel
				 * so we get mode +tmnk instead of +tmnk <key>*/
				//XXX maybe i actually did mean '>=' here, as stated in the comment. test it. not tested it but considered its okay. we did mean > indeed.
				arg = (i > argcount) ? ("*") : arr[i++];
				break;
			case CHANMODE_CLASS_B:
				arg = (i > argcount) ? ("*") : arr[i++];
				break;
			case CHANMODE_CLASS_C:
				if (enable)
					arg = (i > argcount) ? ("*") : arr[i++];
				break;
			case CHANMODE_CLASS_D:
				break;
			default:/*error?*/
				if (strchr(modepfx005chr, c)) {
					arg = (i > argcount) ? ("*") : arr[i++];
				} else {
					WX("unknown chanmode '%c' (0x%X)\n",c,(unsigned)c);
					ptr++;
					continue;
				}
			}
		}
		if (arg)
			WVX("arg is '%s'", arg);
		modearr[j] = malloc((3 + ((arg != NULL) ? strlen(arg) + 1 : 0)));
		modearr[j][0] = enable ? '+' : '-';
		modearr[j][1] = c;
		modearr[j][2] = arg ? ' ' : '\0';
		if (arg) {
			strcpy(modearr[j] + 3, arg);
		}
		j++;
		ptr++;
	}
	WVX("done parsing, result:");
	for(size_t i = 0; i < nummodes; i++) {
		WVX("modearr[%zu]: '%s'", i, modearr[i]);
	}

	*num = nummodes;
	free(modes);
	return modearr;
}

static int
classify_chanmode(char c, const char *const *chmodes)
{
    for (int z = 0; z < 4; ++z) {
        if ((chmodes[z]) && (strchr(chmodes[z], c) != NULL))
		return z+1;/*XXX this locks the chantype class constants */
    }
    return 0;
}
