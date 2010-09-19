/* $Id$
   Copyright (c) Brett England, 2010
 
   No commercial use.
   No redistribution at profit.
   All derivative work must retain this message and
   acknowledge the work of the original author.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xap.h"
#include "bsc.h"

static char *io_str[] = {"input","output"};
static char *state_str[] = {"off","on","?","?"};

/// Decode allowable values for a "state=value" key pair.
static int decode_state(char *msg)
{
        int i;
        static char *value[] = {
                                       "on","off","true","false","yes","no","1","0","toggle"
                               };
        static int state[] = {
                                     1,0,1,0,1,0,1,0,2
                             };
        if (msg == NULL)
                return -1;
        for(i=0; i < sizeof(value); i++) {
                if(strcasecmp(msg, value[i]) == 0)
                        return state[i];
        }
        return -1;
}

/// Fully qualified endpoint name.
static char *bscFQEN(char *target, bscEndpoint *e)
{
        char addr[512];
        strlcpy(addr, target, sizeof addr);
        strlcat(addr, ":", sizeof addr);
        strlcat(addr, e->name, sizeof addr);
        if(e->subaddr) {
                strlcat(addr, ".", sizeof addr);
                strlcat(addr, e->subaddr, sizeof addr);
        }
        return strdup(addr);
}

/// Set the value of a BSC STREAM device type
void setbscText(bscEndpoint *e, char *text)
{
        if(e->text)
                free(e->text);
        e->text = strdup(text);
	info("text=%d", e->text);	
}

/// Set the value of a BSC LEVEL device type.
inline void setbscLevel(bscEndpoint *e, char *level)
{
        setbscText(e, level);
}

/// Set the value of a BSC BINARY device type.
inline void setbscState(bscEndpoint *e, int state)
{
        e->state = state & 0x3;
	info("state=%d", e->state);
}

/// Return a string representation of the state
inline char *bscStateToString(bscEndpoint *e)
{
        return state_str[e->state];
}

/// Return a string representation of the IO type
inline char *bscIOToString(bscEndpoint *e)
{
        return io_str[e->io];
}

/// Locate an Endpoint given its name and optional sub-address.
bscEndpoint *findbscEndpoint(bscEndpoint *head, char *name, char *subaddr)
{
	debug("name %s subaddr %s", subaddr);
        bscEndpoint *e = head;
        while(e) {
                if(strcmp(e->name, name) == 0 &&
                                ((e->subaddr == NULL && subaddr == NULL) ||
                                 (e->subaddr && subaddr && strcmp(e->subaddr, subaddr) == 0))) {
	                info("Found ", e->source);
                        return e;
                }
                e = e->next;
        }
        return NULL;
}

/** Parse BSC LEVEL device type value.
 *
 * An XAP BSC level message may be of the form.
 *     level = <value>/<range>  i.e: 256/512 = 50
 */
int bscParseLevel(char *str)
{
        int level;
        int range;
        if(index(str,'/') == 0) {
	        warning("level=%s", str);
                return 0;
        }
        sscanf(str,"%d/%d", &level, &range);
        return range / level;
}

/** Handle an incoming xapBSC.cmd class control message
 * directed at an endpoint or a set of endpoints.
 */
static void bscIncomingCmd(xAP *xap, void *data)
{
        bscEndpoint *e = (bscEndpoint *)data;
	info("xapBSC.cmd detected for %s", e->source);
	
        // Can't control an INPUT device!
	if(e->io == BSC_INPUT) {
		warning("INPUT devices can't be controlled");
                return;
	}
        // Walk each multi block section updating the ENDPOINT if the section ID allows it.
        char section[16];
        int i;
        for(i=1; i<99; i++) {
                sprintf(section, "output.state.%d", i);
                // State/ID are mandatory items for ALL BSC commands
                char *state = xapGetValue(xap, section, "state");
                char *id = xapGetValue(xap, section, "id");
                if(state == NULL || id == NULL)
                        break;  // malformed section.

                // Match the ENDPOINT ID (UID sub-address) and process.
                if(*id == '*' || strcmp(e->id, id) == 0) {
                        int istate = decode_state(state);
                        if(istate == -1)
                                continue; // invalid state
                        if(istate == 2) // toggle
                                istate = e->state == STATE_ON ? STATE_OFF : STATE_ON;
                        e->state = istate;

                        if(e->type == BSC_LEVEL) {
                                setbscLevel(e, xapGetValue(xap, section, "level"));
                        } else if(e->type == BSC_STREAM) {
                                setbscText(e, xapGetValue(xap, section, "text"));
                        }

                        // Perform Endpoint action.
                        if(e->cmd)
                                (*e->cmd)(xap, e);
                        // Send event.
                        if(e->infoEvent)
                                (*e->infoEvent)(xap, e, "xapBSC.event");
                }
        }
}

/// Send an xapBSC.info message for this endpoint.
static void bscIncomingQuery(xAP *xap, void *data)
{
        bscEndpoint *e = (bscEndpoint *)data;
	info("xapBSC.query detected for %s", e->source);	
	if(e->infoEvent) {
                e->last_report = time(NULL);
                (*e->infoEvent)(xap, e, "xapBSC.info");
        }
}

/** Send XAP INFO message for this endpoint (TIMEOUT).
*
* To reduce traffic if the device is sending EVENTS then we postpone
* the regular INFO message until the INTERVAL time has elasped and no
* xapBSC.query or xapBSC.event has been seen.
*/
static void bscInfoTimeout(xAP *xap, int interval, void *data)
{
        bscEndpoint *e = (bscEndpoint *)data;
        time_t now = time(NULL);
        if(e->infoEvent && (e->last_report = 0 || e->last_report + interval < now )) {
	        info("Timeout for %s", e->source);
	        e->last_report = now;
                (*e->infoEvent)(xap, e, "xapBSC.info");
        }
}

//////////////////////////////////////////////////////////////////////////////////////////////

void bscInfoEvent(xAP *xap, bscEndpoint *e, char *clazz)
{
        int len;

        char buff[XAP_DATA_LEN];
        len = snprintf(buff, XAP_DATA_LEN, "xap-header\n"
                       "{\n"
                       "v=12\n"
                       "hop=1\n"
                       "uid=%s\n"
                       "class=%s\n"
                       "source=%s\n"
                       "}\n"
                       "%s.state\n"
                       "{\n"
                       "state=%s\n",
                       e->uid, clazz, e->source, bscIOToString(e), bscStateToString(e));

        if(e->type == BSC_LEVEL) {
                len += snprintf(&buff[len], XAP_DATA_LEN-len, "level=%s\n", e->level);
        } else if(e->type == BSC_STREAM) {
                len += snprintf(&buff[len], XAP_DATA_LEN-len, "text=%s\n", e->level);
        }
        if(e->displayText) {
                len += snprintf(&buff[len], XAP_DATA_LEN-len, "displaytext=%s\n", e->displayText);
        }
        len += snprintf(&buff[len], XAP_DATA_LEN-len, "}\n");

        if(len > XAP_DATA_LEN)
                err("xAP message buffer truncated - not sending\n");
        else
                xapSend(xap, buff);
}

/// Create a BSC endpoint these are use for automation control.
void bscAddEndpoint(bscEndpoint **head, char *name, char *subaddr, char *id, unsigned int dir, unsigned int typ,
                    void (*cmd)(xAP *xap, struct _bscEndpoint *self),
                    void (*infoEvent)(xAP *xap, struct _bscEndpoint *self, char *clazz)
                   )
{
	die_if(name == NULL, "Name is mandatory");
        die_if(id == NULL || strlen(id) != 2,"Endpoint ID must be length 2");
        bscEndpoint *e = (bscEndpoint *)calloc(1, sizeof(bscEndpoint));

	e->name = strdup(name);
        if(subaddr)
                e->subaddr = strdup(subaddr);
        e->id = strdup(id);
        e->io = dir;
        e->type = typ;
        e->state = STATE_ON;
        if(e->io == BSC_OUTPUT)
                e->state = STATE_UNKNOWN;
        e->cmd = cmd;
        // If the user hasn't supplied handlers use the defaults.
        e->infoEvent = infoEvent == NULL ? bscInfoEvent : infoEvent;

        e->next = *head;
        *head = e;
}

/// Add CMD and QUERY filter callbacks for the BSC endpoints.
void xapAddBscEndpointFilters(xAP *xap, bscEndpoint *head, int info_interval)
{
        xAPFilter *filter;
	die_if(xap == NULL, "xAP xap==NULL");
	notice_if(head == NULL, "No endpoints to add!?");
        while(head) {
                // UID of the endpoint is the xAP UID with the latest 2 digits replaced with the endpoint ID.
                head->uid = strdup(xap->uid);
                strncpy(&head->uid[6], head->id, 2);

                // Compute ENDPOINTS source name.
                head->source = bscFQEN(xap->source, head);
        	info("source=%s", head->source);

                if(head->io == BSC_OUTPUT) {
                        filter = NULL;
                        xapAddFilter(&filter, "xap-header","class","xapBSC.cmd");
                        xapAddFilter(&filter, "xap-header","target", head->source);
                        xapAddFilterAction(xap, &bscIncomingCmd, filter, head);
                }

                filter = NULL;
                xapAddFilter(&filter, "xap-header","class","xapBSC.query");
                xapAddFilter(&filter, "xap-header","target", head->source);
                xapAddFilterAction(xap, &bscIncomingQuery, filter, head);

                if(info_interval > 0) {
                        xapAddTimeoutAction(xap, &bscInfoTimeout, info_interval, head);
                }

                head = head->next;
        }
}
