#include "api.h"

void ApiLogsGetList(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiLogsGet(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiLogsDelete(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NO_CONTENT;
    }
}

void ApiLogsDeleteAll(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NO_CONTENT;
    }
}
