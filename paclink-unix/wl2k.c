/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 2; c-brace-offset: -2; c-argdecl-indent: 2 -*- */

/*  paclink-unix client for the Winlink 2000 ham radio email system.
 *
 *  Copyright 2006 Nicholas S. Castellano <n2qz@arrl.net> and others,
 *                 See the file AUTHORS for a list.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifdef __RCSID
__RCSID("$Id$");
#endif

#if HAVE_STDIO_H
# include <stdio.h>
#endif
#if HAVE_STDLIB_H
# include <stdlib.h>
#endif
#if HAVE_STRING_H
# include <string.h>
#endif
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if HAVE_CTYPE_H
# include <ctype.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if HAVE_SYS_ERRNO_H
# include <sys/errno.h>
#endif
#if HAVE_ERRNO_H
# include <errno.h>
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) (strlen((dirent)->d_name))
#else
# define dirent direct
# define NAMLEN(dirent) ((dirent)->d_namlen)
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include "compat.h"
#include "strutil.h"
#include "wl2k.h"
#include "timeout.h"
#include "mid.h"
#include "buffer.h"
#include "lzhuf_1.h"
#include "wl2mime.h"

#define PROPLIMIT 5

struct proposal {
  char code;
  char type;
  char mid[MID_MAXLEN + 1];
  unsigned long usize;
  unsigned long csize;
  struct proposal *next;
  char *path;
  struct buffer *ubuf;
  struct buffer *cbuf;
  char *title;
  unsigned long offset;
  int accepted;
  int delete;
};

static int getrawchar(FILE *fp);
static struct buffer *getcompressed(FILE *fp);
static struct proposal *parse_proposal(char *propline);
static int b2outboundproposal(FILE *fp, char *lastcommand, struct proposal **oproplist);
static void printprop(struct proposal *prop);
static void putcompressed(struct proposal *prop, FILE *fp);
static char *tgetline(FILE *fp, int terminator, int ignore);
static void dodelete(struct proposal **oproplist, struct proposal **nproplist);

static int
getrawchar(FILE *fp)
{
  int c;

  resettimeout();
  c = fgetc(fp);
  if (c == EOF) {
    fprintf(stderr, "%s: lost connection in getrawchar()\n", getprogname());
    exit(EXIT_FAILURE);
  }
  return c;
}

#define CHRNUL 0
#define CHRSOH 1
#define CHRSTX 2
#define CHREOT 4

struct buffer *
getcompressed(FILE *fp)
{
  int c;
  int len;
  int i;
  unsigned char title[81];
  unsigned char offset[7];
  int cksum = 0;
  struct buffer *buf;

  if ((buf = buffer_new()) == NULL) {
    return NULL;
  }
  c = getrawchar(fp);
  if (c != CHRSOH) {
    buffer_free(buf);
    return NULL;
  }
  len = getrawchar(fp);
  title[80] = '\0';
  for (i = 0; i < 80; i++) {
    c = getrawchar(fp);
    len--;
    title[i] = (unsigned char)c;
    if (c == CHRNUL) {
      ungetc(c, fp);
      len++;
      break;
    }
  }
  c = getrawchar(fp);
  len--;
  if (c != CHRNUL) {
    buffer_free(buf);
    return NULL;
  }
  fprintf(stderr, "%s: title: %s\n", getprogname(), title);
  offset[6] = '\0';
  for (i = 0; i < 6; i++) {
    c = getrawchar(fp);
    len--;
    offset[i] = (unsigned char)c;
    if (c == CHRNUL) {
      ungetc(c, fp);
      len++;
      break;
    }
  }
  c = getrawchar(fp);
  len--;
  if (c != CHRNUL) {
    buffer_free(buf);
    return NULL;
  }
  fprintf(stderr, "%s: offset: %s\n", getprogname(), offset);
  if (len != 0) {
    buffer_free(buf);
    return NULL;
  }
  if (strcmp((const char *) offset, "0") != 0) {
    buffer_free(buf);
    return NULL;
  }

  for (;;) {
    c = getrawchar(fp);
    switch (c) {
    case CHRSTX:
      fprintf(stderr, "%s: STX\n", getprogname());
      len = getrawchar(fp);
      if (len == 0) {
	len = 256;
      }
      fprintf(stderr, "%s: len %d\n", getprogname(), len);
      while (len--) {
	c = getrawchar(fp);
	if (buffer_addchar(buf, c) == EOF) {
	  buffer_free(buf);
	  return NULL;
	}
	cksum = (cksum + c) % 256;
      }
      break;
    case CHREOT:
      fprintf(stderr, "%s: EOT\n", getprogname());
      c = getrawchar(fp);
      cksum = (cksum + c) % 256;
      if (cksum != 0) {
	fprintf(stderr, "%s: bad cksum\n", getprogname());
	buffer_free(buf);
	return NULL;
      }
      return buf;
      break;
    default:
      fprintf(stderr, "%s: unexpected character in compressed stream\n", getprogname());
      buffer_free(buf);
      return NULL;
      break;
    }
  }
  buffer_free(buf);
  return NULL;
}

static void
putcompressed(struct proposal *prop, FILE *fp)
{
  size_t len;
  unsigned char title[81];
  unsigned char offset[7];
  int cksum = 0;
  unsigned char *cp;
  long rem;
  unsigned char msglen;

  strlcpy((char *) title, prop->title, sizeof(title));
  snprintf((char *) offset, sizeof(offset), "%lu", prop->offset);

  fprintf(stderr, "%s: transmitting [%s] [offset %s]\n", getprogname(), title, offset);

  len = strlen((const char *) title) + strlen((const char *) offset) + 2;

  /* ** Send hearder */
  resettimeout();
  if (fprintf(fp, "%c%c%s%c%s%c", CHRSOH, len, title, CHRNUL, offset, CHRNUL) == -1) {
    perror("fprintf()");
    exit(EXIT_FAILURE);
  }
  fflush(fp);

  rem = (long)prop->csize;
  cp = prop->cbuf->data;

  if (rem < 6) {
    fprintf(stderr, "%s: invalid compressed data\n", getprogname());
    exit(EXIT_FAILURE);
  }

#if 0 /* why??? */
  {
    int i;

    resettimeout();
    if (fprintf(fp, "%c%c", CHRSTX, 6) == -1) {
      perror("fprintf()");
      exit(EXIT_FAILURE);
    }
    for (i = 0; i < 6; i++) {
      resettimeout();
      cksum += *cp;
      if (fputc(*cp++, fp) == EOF) {
        perror("fputc()");
        exit(EXIT_FAILURE);
      }
    }
    rem -= 6;
  }
#endif /* why */

  cp += prop->offset;
  rem -= (long)prop->offset;

  if (rem < 0) {
    fprintf(stderr, "%s: invalid offset\n", getprogname());
    exit(EXIT_FAILURE);
  }

  /* ** Send message */
  while (rem > 0) {
    fprintf(stderr, "%s: ... %ld\n", getprogname(), rem);
    if (rem > 250) {
      msglen = 250;
    } else {
      msglen = (unsigned char)rem;
    }
    if (fprintf(fp, "%c%c", CHRSTX, msglen) == -1) {
      perror("fprintf()");
      exit(EXIT_FAILURE);
    }

    /* ** send buffer to ax25 stack */
    while (msglen--) {
      resettimeout();
      cksum += *cp;
      if (fputc(*cp++, fp) == EOF) {
        perror("fputc()");
        exit(EXIT_FAILURE);
      }
      rem--;
    }
    fflush(fp);
  }

  /* ** Send checksum */
  cksum = -cksum & 0xff;
  resettimeout();
  if (fprintf(fp, "%c%c", CHREOT, cksum) == -1) {
    perror("fprintf()");
    exit(EXIT_FAILURE);
  }
  fflush(fp);
  resettimeout();
}

static struct proposal *
parse_proposal(char *propline)
{
  char *cp = propline;
  static struct proposal prop;
  int i;
  char *endp;

  if (!cp) {
    return NULL;
  }
  if (*cp++ != 'F') {
    return NULL;
  }
  prop.code = *cp++;
  switch (prop.code) {
  case 'C':
    if (*cp++ != ' ') {
      fprintf(stderr, "%s: malformed proposal 1\n", getprogname());
      return NULL;
    }
    prop.type = *cp++;
    if ((prop.type != 'C') && (prop.type != 'E')) {
      fprintf(stderr, "%s: malformed proposal 2\n", getprogname());
      return NULL;
    }
    if (*cp++ != 'M') {
      fprintf(stderr, "%s: malformed proposal 3\n", getprogname());
      return NULL;
    }
    if (*cp++ != ' ') {
      fprintf(stderr, "%s: malformed proposal 4\n", getprogname());
      return NULL;
    }
    for (i = 0; i < MID_MAXLEN; i++) {
      prop.mid[i] = *cp++;
      if (prop.mid[i] == ' ') {
	prop.mid[i] = '\0';
	cp--;
	break;
      } else {
	if (prop.mid[i] == '\0') {
	  fprintf(stderr, "%s: malformed proposal 5\n", getprogname());
	  return NULL;
	}
      }
    }
    prop.mid[MID_MAXLEN] = '\0';
    if (*cp++ != ' ') {
      fprintf(stderr, "%s: malformed proposal 6\n", getprogname());
      return NULL;
    }
    prop.usize = strtoul(cp, &endp, 10);
    cp = endp;
    if (*cp++ != ' ') {
      fprintf(stderr, "%s: malformed proposal 7\n", getprogname());
      return NULL;
    }
    prop.csize = (unsigned int) strtoul(cp, &endp, 10);
    cp = endp;
    if (*cp != ' ') {
      fprintf(stderr, "%s: malformed proposal 8\n", getprogname());
      return NULL;
    }
    break;
  case 'A':
  case 'B':
  default:
    prop.type = 'X';
    prop.mid[0] = '\0';
    prop.usize = 0;
    prop.csize = 0;
    break;
    fprintf(stderr, "%s: unsupported proposal type %c\n", getprogname(), prop.code);
    break;
  }
  prop.next = NULL;
  prop.path = NULL;
  prop.cbuf = NULL;
  prop.ubuf = NULL;
  prop.delete = 0;

  return &prop;
}

static void
printprop(struct proposal *prop)
{
  fprintf(stderr,
	  "%s: proposal code %c type %c mid %s usize %lu csize %lu next %p path %s ubuf %p cbuf %p\n",
	  getprogname(),
	  prop->code,
	  prop->type,
	  prop->mid,
	  prop->usize,
	  prop->csize,
	  prop->next,
	  prop->path,
	  prop->ubuf,
	  prop->cbuf);
}

static void
dodelete(struct proposal **oproplist, struct proposal **nproplist)
{
  if ((oproplist == NULL) || (nproplist == NULL)) {
    fprintf(stderr, "%s: bad call to dodelete()\n", getprogname());
    exit(EXIT_FAILURE);
  }
  while (*oproplist != *nproplist) {
    if (((*oproplist)->delete) && ((*oproplist)->path)) {
      fprintf(stderr, "%s: DELETING PROPOSAL: ", getprogname());
      printprop(*oproplist);
#if 1
      if(unlink((*oproplist)->path) < 0) {
        fprintf(stderr, "%s: Can't delete file: %s: %s\n",
                getprogname(), (*oproplist)->path, strerror(errno));
      }
#endif
      (*oproplist)->delete = 0;
    }
    oproplist = &((*oproplist)->next);
  }
}

static struct proposal *
prepare_outbound_proposals(void)
{
  struct proposal *prop;
  struct proposal **opropnext;
  struct proposal *oproplist = NULL;
  DIR *dirp;
  struct dirent *dp;
  char *line;
  unsigned char *cp;
  struct stat sb;
  char name[MID_MAXLEN + 1];

  opropnext = &oproplist;
  if (chdir(WL2K_OUTBOX) != 0) {
    fprintf(stderr, "%s: chdir(%s): %s\n", getprogname(), WL2K_OUTBOX, strerror(errno));
    exit(EXIT_FAILURE);
  }
  if ((dirp = opendir(".")) == NULL) {
    perror("opendir()");
    exit(EXIT_FAILURE);
  }
  while ((dp = readdir(dirp)) != NULL) {
    if (NAMLEN(dp) > MID_MAXLEN) {
      fprintf(stderr,
	      "%s: warning: skipping bad filename %s in outbox directory %s\n",
	      getprogname(), dp->d_name, WL2K_OUTBOX);
      continue;
    }
    strlcpy(name, dp->d_name, MID_MAXLEN + 1);
    name[NAMLEN(dp)] = '\0';
    if (stat(name, &sb)) {
      continue;
    }
    if (!(S_ISREG(sb.st_mode))) {
      continue;
    }
    if ((prop = malloc(sizeof(struct proposal))) == NULL) {
      perror("malloc()");
      exit(EXIT_FAILURE);
    }
    prop->code = 'C';
    prop->type = 'E';
    strlcpy(prop->mid, name, MID_MAXLEN + 1);
    prop->path = strdup(name);

    if ((prop->ubuf = buffer_readfile(prop->path)) == NULL) {
      perror(prop->path);
      exit(EXIT_FAILURE);
    }
    prop->usize = prop->ubuf->dlen;

    if ((prop->cbuf = version_1_Encode(prop->ubuf)) == NULL) {
      perror("version_1_Encode()");
      exit(EXIT_FAILURE);
    }

    prop->csize = (unsigned long) prop->cbuf->dlen;

    prop->title = NULL;
    prop->offset = 0;
    prop->accepted = 0;
    prop->delete = 0;

    buffer_rewind(prop->ubuf);
    while ((line = buffer_getline(prop->ubuf, '\n')) != NULL) {
      if (strbegins(line, "Subject:")) {
	strzapcc(line);
	cp = ((unsigned char *) line) + 8;
	while (isspace(*cp)) {
	  cp++;
	}
	if (strlen((const char *) cp) > 80) {
	  cp[80] = '\0';
	}
	prop->title = strdup((const char *) cp);
      }
      free(line);
    }

    if (prop->title == NULL) {
      prop->title = strdup("No subject");
    }

    prop->next = NULL;

    *opropnext = prop;
    opropnext = &prop->next;
  }
  closedir(dirp);

  printf("---");
  printf("%s\n", oproplist ? " outbound proposal list" : "");

  for (prop = oproplist; prop != NULL; prop = prop->next) {
    printprop(prop);
  }

  return oproplist;
}

static int
b2outboundproposal(FILE *fp, char *lastcommand, struct proposal **oproplist)
{
  int i;
  char *sp;
  unsigned char *cp;
  int cksum = 0;
  char *line;
  struct proposal *prop;
  char *endp;

  if (*oproplist) {
    prop = *oproplist;
    for (i = 0; i < PROPLIMIT; i++) {
      if (asprintf(&sp, "F%c %cM %s %lu %lu 0\r",
		  prop->code,
		  prop->type,
		  prop->mid,
		  prop->usize,
		  prop->csize) == -1) {
	perror("asprintf()");
	exit(EXIT_FAILURE);
      }
      printf(">%s\n", sp);
      resettimeout();
      if (fprintf(fp, "%s", sp) == -1) {
	perror("fprintf()");
	exit(EXIT_FAILURE);
      }
      for (cp = (unsigned char *) sp; *cp; cp++) {
	cksum += (unsigned char) *cp;
      }
      free(sp);
      if ((prop = prop->next) == NULL) {
	break;
      }
    }
    cksum = -cksum & 0xff;
    printf(">F> %2X\n", cksum);
    resettimeout();
    if (fprintf(fp, "F> %2X\r", cksum) == -1) {
      perror("fprintf()");
      exit(EXIT_FAILURE);
    }
    if ((line = wl2kgetline(fp)) == NULL) {
      fprintf(stderr, "%s: connection closed\n", getprogname());
      exit(EXIT_FAILURE);
    }
    printf("<%s\n", line);

    if (!strbegins(line, "FS ")) {
      fprintf(stderr, "%s: b2 protocol error 1\n", getprogname());
      exit(EXIT_FAILURE);
    }
    prop = *oproplist;
    i = 0;
    cp = ((unsigned char *) line) + 3;
    while (*cp && isspace(*cp)) {
      cp++;
    }
    while (*cp && prop) {
      if (i == PROPLIMIT) {
	fprintf(stderr, "%s: B2 protocol error 2\n", getprogname());
	exit(EXIT_FAILURE);
      }
      prop->accepted = 0;
      prop->delete = 0;
      prop->offset = 0;
      switch(*cp) {
      case 'Y': case 'y':
      case 'H': case 'h':
      case '+':
	prop->accepted = 1;
	break;
      case 'N': case 'n':
      case 'R': case 'r':
      case '-':
	prop->delete = 1;
	break;
      case 'L': case 'l':
      case '=':
	break;
      case 'A': case 'a':
      case '!':
	prop->accepted = 1;
	prop->offset = strtoul((const char *) cp, &endp, 10);
	cp = ((unsigned char *) endp) - 1;
	break;
      default:
	fprintf(stderr, "%s: B2 protocol error 3\n", getprogname());
	exit(EXIT_FAILURE);
	break;
      }
      cp++;
      prop = prop->next;
    }

    prop = *oproplist;
    for (i = 0; i < PROPLIMIT; i++) {
      putcompressed(prop, fp);
      prop->delete = 1;
      if ((prop = prop->next) == NULL) {
	break;
      }
    }
    *oproplist = prop;
    return 0;
  } else if (strbegins(lastcommand, "FF")) {
    printf(">FQ\n");
    resettimeout();
    if (fprintf(fp, "FQ\r") == -1) {
      perror("fprintf()");
      exit(EXIT_FAILURE);
    }
    return -1;
  } else {
    printf(">FF\n");
    resettimeout();
    if (fprintf(fp, "FF\r") == -1) {
      perror("fprintf()");
      exit(EXIT_FAILURE);
    }
    return 0;
  }
}

static char *
tgetline(FILE *fp, int terminator, int ignore)
{
  static struct buffer *buf = NULL;
  int c;

  if (buf == NULL) {
    if ((buf = buffer_new()) == NULL) {
      perror("buffer_new()");
      exit(EXIT_FAILURE);
    }
  } else {
    buffer_truncate(buf);
  }
  for (;;) {
    resettimeout();
    if ((c = fgetc(fp)) == EOF) {
      return NULL;
    }
    if (c == terminator) {
      if (buffer_addchar(buf, '\0') == -1) {
	perror("buffer_addchar()");
	exit(EXIT_FAILURE);
      }
      return (char *) buf->data;
    }
    if ((c != ignore) && buffer_addchar(buf, c) == -1) {
      perror("buffer_addchar()");
      exit(EXIT_FAILURE);
    }
  }
  return NULL;
}

char *
wl2kgetline(FILE *fp)
{
  char *cp;

  cp = tgetline(fp, '\r', '\n');
  return cp;
}

void
wl2kexchange(char *mycall, char *yourcall, FILE *fp, char *emailaddress)
{
  char *cp;
  int proposals = 0;
  int proposalcksum = 0;
  int i;
  char sidbuf[32];
  char *inboundsid = NULL;
  char *inboundsidcodes = NULL;
  char *line;
  struct proposal *prop;
  struct proposal ipropary[PROPLIMIT];
  struct proposal *oproplist;
  struct proposal *nproplist;
  unsigned long sentcksum;
  char *endp;
  int opropcount = 0;
  char responsechar;
  FILE *smfp;
  struct buffer *mimebuf;
  int c;
  int r;
  char *command;

  if (expire_mids() == -1) {
    fprintf(stderr, "%s: expire_mids() failed\n", getprogname());
    exit(EXIT_FAILURE);
  }

  oproplist = prepare_outbound_proposals();

  for (prop = oproplist; prop; prop = prop->next) {
    opropcount++;
  }

  while ((line = wl2kgetline(fp)) != NULL) {
    printf("<%s\n", line);
    if (strchr(line, '[')) {
      inboundsid = strdup(line);
      if ((cp = strrchr(inboundsid, '-')) == NULL) {
	fprintf(stderr, "%s: bad sid %s\n", getprogname(), inboundsid);
	exit(EXIT_FAILURE);
      }
      inboundsidcodes = strdup(cp);
      if ((cp = strrchr(inboundsidcodes, ']')) == NULL) {
	fprintf(stderr, "%s: bad sid %s\n", getprogname(), inboundsid);
	exit(EXIT_FAILURE);
      }
      *cp = '\0';
      strupper(inboundsidcodes);
      if (strstr(inboundsidcodes, "B2F") == NULL) {
	fprintf(stderr, "%s: sid %s does not support B2F protocol\n", getprogname(), inboundsid);
	exit(EXIT_FAILURE);
      }
      fprintf(stderr, "%s: sid %s inboundsidcodes %s\n", getprogname(), inboundsid, inboundsidcodes);

    } else if (line[strlen(line) - 1] == '>') {
      if (inboundsidcodes == NULL) {
	fprintf(stderr, "%s: inboundsidcodes not set\n", getprogname());
	exit(EXIT_FAILURE);
      }
      if (strchr(inboundsidcodes, 'I')) {
	printf(">; %s DE %s QTC %d\n", yourcall, mycall, opropcount);
	resettimeout();
	if (fprintf(fp, "; %s DE %s QTC %d\r", yourcall, mycall, opropcount) == -1) {
	  perror("fprintf()");
	  exit(EXIT_FAILURE);
	}
      }
      sprintf(sidbuf, "[PaclinkUNIX-%s-B2FIHM$]", PACKAGE_VERSION);
      printf(">%s\n", sidbuf);
      resettimeout();
      if (fprintf(fp, "%s\r", sidbuf) == -1) {
	perror("fprintf()");
	exit(EXIT_FAILURE);
      }
      break;
    }
  }
  if (line == NULL) {
    fprintf(stderr, "%s: Lost connection. 1\n", getprogname());
    exit(EXIT_FAILURE);
  }

  nproplist = oproplist;
  if (b2outboundproposal(fp, line, &nproplist) != 0) {
    return;
  }

  while ((line = wl2kgetline(fp)) != NULL) {
    printf("<%s\n", line);
    if (strbegins(line, ";")) {
      /* do nothing */
    } else if (strlen(line) == 0) {
      /* do nothing */
    } else if (strbegins(line, "FC")) {
      dodelete(&oproplist, &nproplist);
      for (cp = line; *cp; cp++) {
	proposalcksum += (unsigned char) *cp;
      }
      proposalcksum += '\r'; /* bletch */
      if (proposals == PROPLIMIT) {
	fprintf(stderr, "%s: too many proposals\n", getprogname());
	exit(EXIT_FAILURE);
      }
      if ((prop = parse_proposal(line)) == NULL) {
	fprintf(stderr, "%s: failed to parse proposal\n", getprogname());
	exit(EXIT_FAILURE);
      }
      memcpy(&ipropary[proposals], prop, sizeof(struct proposal));
      printprop(&ipropary[proposals]);
      proposals++;
    } else if (strbegins(line, "FF")) {
      dodelete(&oproplist, &nproplist);
      if (b2outboundproposal(fp, line, &nproplist) != 0) {
	return;
      }
    } else if (strbegins(line, "B")) {
      return;
    } else if (strbegins(line, "FQ")) {
      dodelete(&oproplist, &nproplist);
      return;
    } else if (strbegins(line, "F>")) {
      proposalcksum = (-proposalcksum) & 0xff;
      sentcksum = strtoul(line + 2, &endp, 16);

      fprintf(stderr, "%s: sentcksum=%lX proposalcksum=%lX\n", getprogname(), sentcksum, (unsigned long) proposalcksum);
      if (sentcksum != (unsigned long) proposalcksum) {
	fprintf(stderr, "%s: proposal cksum mismatch\n", getprogname());
	exit(EXIT_FAILURE);
      }

      fprintf(stderr, "%s: %d proposal%s received\n", getprogname(), proposals, ((proposals == 1) ? "" : "s"));

      if (proposals != 0) {
	printf(">FS ");
	resettimeout();
	if (fprintf(fp, "FS ") == -1) {
	  perror("fprintf()");
	  exit(EXIT_FAILURE);
	}
	for (i = 0; i < proposals; i++) {
	  ipropary[i].accepted = 0;
	  if (ipropary[i].code == 'C') {
	    if (check_mid(ipropary[i].mid)) {
	      responsechar = 'N';
	    } else {
	      responsechar = 'Y';
	      ipropary[i].accepted = 1;
	    }
	  } else {
	    responsechar = 'L';
	  }
	  putchar(responsechar);
	  resettimeout();
	  if (fputc(responsechar, fp) == EOF) {
	    perror("fputc()");
	    exit(EXIT_FAILURE);
	  }
	}
	printf("\n");
	resettimeout();
	if (fprintf(fp, "\r") == -1) {
	  perror("fprintf()");
	  exit(EXIT_FAILURE);
	}

	for (i = 0; i < proposals; i++) {
	  if (ipropary[i].accepted != 1) {
	    continue;
	  }

	  if ((ipropary[i].cbuf = getcompressed(fp)) == NULL) {
	    fprintf(stderr, "%s: error receiving compressed data\n", getprogname());
	    exit(EXIT_FAILURE);
	  }

	  fprintf(stderr, "%s: extracting...\n", getprogname());
	  if ((ipropary[i].ubuf = version_1_Decode(ipropary[i].cbuf)) == NULL) {
	    perror("version_1_Decode()");
	    exit(EXIT_FAILURE);
	  }

#if 0
	  if (buffer_writefile(ipropary[i].mid, ipropary[i].ubuf) != 0) {
	    perror("buffer_writefile()");
	    exit(EXIT_FAILURE);
	  }
#endif

	  buffer_rewind(ipropary[i].ubuf);
	  if ((mimebuf = wl2mime(ipropary[i].ubuf)) == NULL) {
	    fprintf(stderr, "%s: wm2mime() failed\n", getprogname());
	    exit(EXIT_FAILURE);
	  }
	  fflush(NULL);

	  fprintf(stderr, "%s: calling sendmail for delivery\n", getprogname());
	  if (asprintf(&command, "%s %s %s", SENDMAIL, SENDMAIL_FLAGS, emailaddress) == -1) {
	    perror("asprintf()");
	    exit(EXIT_FAILURE);
	  }
	  if ((smfp = popen(command, "w")) == NULL) {
	    perror("popen()");
	    exit(EXIT_FAILURE);
	  }
	  free(command);
	  buffer_rewind(mimebuf);
	  while ((c = buffer_iterchar(mimebuf)) != EOF) {
	    if (putc(c, smfp) == EOF) {
	      exit(EXIT_FAILURE);
	    }
	  }
	  if ((r = pclose(smfp)) != 0) {
	    if (r < 0) {
	      perror("pclose()");
	    } else {
	      fprintf(stderr, "%s: sendmail failed\n", getprogname());
	    }
	    exit(EXIT_FAILURE);
	  }
	  fprintf(stderr, "%s: delivery completed\n", getprogname());
	  record_mid(ipropary[i].mid);
	  buffer_free(ipropary[i].ubuf);
	  ipropary[i].ubuf = NULL;
	  buffer_free(ipropary[i].cbuf);
	  ipropary[i].cbuf = NULL;
	  fprintf(stderr, "%s: Finished!\n", getprogname());
	}
      }
      proposals = 0;
      proposalcksum = 0;
      if (b2outboundproposal(fp, line, &nproplist) != 0) {
	return;
      }
    } else if (line[strlen(line - 1)] == '>') {
      dodelete(&oproplist, &nproplist);
      if (b2outboundproposal(fp, line, &nproplist) != 0) {
	return;
      }
    } else {
      fprintf(stderr, "%s: unrecognized command (len %lu): /%s/\n", getprogname(), (unsigned long) strlen(line), line);
      exit(EXIT_FAILURE);
    }
  }
  if (line == NULL) {
    fprintf(stderr, "%s: Lost connection. 4\n", getprogname());
    exit(EXIT_FAILURE);
  }
}
