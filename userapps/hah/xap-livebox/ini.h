/* $Id$
*/

#ifndef INI_H
#define INI_H

extern bscEndpoint *endpointList;
extern const char inifile[];

void addIniEndpoints();
void infoEventRFnRelay(bscEndpoint *e, char *clazz);

#endif
