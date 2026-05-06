#include "api.h"

void ApiSystemGetStatus(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiSystemGetClock(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiSystemPutClock(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}

void ApiSystemPutReset(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}
