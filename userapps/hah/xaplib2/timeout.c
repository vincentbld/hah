/* $Id$
   Copyright (c) Brett England, 2010
 
   No commercial use.
   No redistribution at profit.
   All derivative work must retain this message and
   acknowledge the work of the original author.
*/
#include <stdlib.h>
#include <time.h>
#include "xap.h"

/** Add a timeout callback to the list.
*/
xAPTimeoutCallback *xapAddTimeoutAction(void (*func)(int, void *), int interval, void *data)
{
	die_if(interval < 1, "Invalid timeout interval %d secs", interval);	
	debug("Add timeout. interval=%d",interval);
	xAPTimeoutCallback *cb = (xAPTimeoutCallback *)malloc(sizeof(xAPTimeoutCallback));
        cb->callback = func;
        cb->user_data = data;
        cb->interval = interval;
        cb->ttl = 0;

        LL_PREPEND(gXAP->timeoutList, cb);
        return cb;
}

/** Remove a timeout and free the memory for the callback.
*/
void xapDelTimeoutAction(xAPTimeoutCallback **cb)
{
        LL_DELETE(gXAP->timeoutList, *cb);
        free(*cb);
	*cb = NULL;
}

/** Remove a timeout using the callback func as a lookup key.
* If multiple timeouts are registered for the same callback func
* ALL will be deleted.
*/
void xapDelTimeoutActionByFunc(void (*func)(int, void *))
{
        xAPTimeoutCallback *e, *tmp;
        LL_FOREACH_SAFE(gXAP->timeoutList, e, tmp) {
                if(e->callback == func)
                        xapDelTimeoutAction(&e);
        }
}

/** Dispatch timeout callback that have expired.
*/
void timeoutDispatch()
{
        xAPTimeoutCallback *cb;
        time_t now = time(NULL);
        LL_FOREACH(gXAP->timeoutList, cb) {
                if(cb->ttl <= now) { // Has the Callbacks timer expired?
                        cb->ttl = now + cb->interval;
                        (*cb->callback)(cb->interval, cb->user_data); // Dispatch it..
                }
        }
}

