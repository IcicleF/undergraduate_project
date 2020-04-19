#if !defined(MSG_HPP)
#define MSG_HPP

#include "../commons.hpp"
#include "../fs/inode.hpp"

struct PureValueRequest { int value; } __packed;
struct ValueWithPathRequest { uint64_t value; int len; char path[MAX_PATH_LEN]; } __packed;
struct RawRequest { int len; char raw[4090]; } __packed;
union GeneralRequest
{
    PureValueRequest pvReq;
    ValueWithPathRequest vwpReq;
    RawRequest rawReq;
} __packed;

struct PureValueResponse { int value; } __packed;
union StatResponse { loco_dir_stat dirStat; loco_file_stat fileStat; } __packed;
union InodeResponse { DirectoryInode di; FileInode fi; } __packed;
struct RawResponse { int len; char raw[4090]; } __packed;
union GeneralResponse
{
    PureValueResponse pvResp;
    StatResponse statResp;
    InodeResponse inodeResp;
    RawResponse rawResp;
} __packed;

#endif // MSG_HPP
