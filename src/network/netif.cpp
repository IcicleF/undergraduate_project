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

    netif->locks[idx].complete();
}

void connectHandler(erpc::ReqHandle *reqHandle, void *context)
{
    auto *netif = reinterpret_cast<NetworkInterface *>(context);
    int sessId = reqHandle->get_local_session_id();
    
    auto *msgBuf = reqHandle->get_req_msgbuf()->buf;
    auto *notifyReq = reinterpret_cast<PureValueRequest *>(msgBuf);
    int peerId = notifyReq->value;
    netif->sessions[peerId] = sessId;
    netif->sess2id[sessId] = peerId;

    d_info("Received connection from: peer %d, session %d", peerId, sessId);

    auto &resp = reqHandle->pre_resp_msgbuf;
    netif->rpc->resize_msg_buffer(&resp, sizeof(PureValueResponse));
    auto *notifyResp = reinterpret_cast<PureValueResponse *>(resp.buf);
    notifyResp->value = myNodeConf->id;

    netif->rpc->enqueue_response(reqHandle, &resp);
}
