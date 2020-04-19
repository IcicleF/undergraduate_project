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
            exit(-1);
    }
}

void contFunc(void *context, void *tag)
{
    auto *netif = reinterpret_cast<NetworkInterface *>(context);
    auto idx = reinterpret_cast<uintptr_t>(tag);

    netif->locks[idx].complete();
}
