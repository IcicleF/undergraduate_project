#include <network/netif.hpp>
#include <debug.hpp>

void smHandler(int sessionNum, erpc::SmEventType event, erpc::SmErrType err, void *context)
{
    switch (event) {
        case erpc::SmEventType::kConnected:
            d_info("eRPC: connected");
            break;
        case erpc::SmEventType::kConnectFailed:
            d_info("eRPC: connect failed");
            break;
        case erpc::SmEventType::kDisconnected:
            d_info("eRPC: disconnected");
            break;
        case erpc::SmEventType::kDisconnectFailed:
            d_info("eRPC: disconnect failed");
            break;
        default:
            d_err("unknown eRPC event type");
            exit(-1);
    }
}

void contFunc(void *context, void *tag)
{
    auto *netif = reinterpret_cast<NetworkInterface *>(context);
    auto idx = reinterpret_cast<uintptr_t>(tag);

    //d_info("contFunc triggered, response from %d", static_cast<int>(idx));

    netif->locks[idx].complete();
}

void dummyContFunc(void *, void *)
{
    d_info("dummy cont func triggered");
}

/* This function is now useless */
void connectHandler(erpc::ReqHandle *reqHandle, void *context)
{
    auto *req = interpretRequest<PureValueRequest>(reqHandle);
    int peerId = static_cast<int>(req->value);

    d_info("Received connection from: peer %d", peerId);

    auto *resp = allocateResponse<PureValueResponse>(reqHandle, context);
    resp->value = myNodeConf->id;
    sendResponse(reqHandle, context);
}

void dummyHandler(erpc::ReqHandle *req_handle, void *context) {
    auto rpc = reinterpret_cast<NetworkInterface *>(context)->getRPC();

    auto &resp = req_handle->pre_resp_msgbuf;
    rpc->resize_msg_buffer(&resp, sizeof(PureValueResponse));
    rpc->enqueue_response(req_handle, &resp);
}

void sendResponse(erpc::ReqHandle *reqHandle, void *context)
{
    auto *netif = reinterpret_cast<NetworkInterface *>(context);
    netif->getRPC()->enqueue_response(reqHandle, &reqHandle->pre_resp_msgbuf);
}
