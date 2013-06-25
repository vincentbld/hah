/* $Id$
 */
#ifndef __DSCACHE_H__
#define __DSCACHE_H__

struct datastream {
  unsigned int feed;
  unsigned int id;
  char *tag;
  float value;
  char *min;
  char *max;
  char *unit;
};

void updateDatastream(unsigned int feed, unsigned int id, char *tag, float value, char *min, char *max, char *unit);
void xivelyWebUpdate(int interval, void *userData);

#endif