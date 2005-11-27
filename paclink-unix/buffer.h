/* $Id$ */

#ifndef BUFFER_H
#define BUFFER_H

struct buffer {
  unsigned char *data;
  unsigned long alen;
  unsigned long dlen;
  unsigned int i;
};

struct buffer *buffer_new(void);
void buffer_free(struct buffer *b);
int buffer_addchar(struct buffer *b, int c);
int buffer_addstring(struct buffer *b, const unsigned char *s);
int buffer_setstring(struct buffer *b, const unsigned char *s);
void buffer_rewind(struct buffer *b);
int buffer_iterchar(struct buffer *b);
int buffer_lastchar(struct buffer *b);
void buffer_truncate(struct buffer *b);

#endif