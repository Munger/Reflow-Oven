#include "api.h"

void ApiConfigGet(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiConfigPut(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}
