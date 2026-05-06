#include "api.h"

void ApiStorageGet(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_NOT_FOUND;
    }
}

void ApiStoragePutFormat(APIPBPtr pb)
{
    (void)pb;
    if (pb) {
        pb->status = API_STATUS_OK;
    }
}
