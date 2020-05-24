#if !defined(MSG_HPP)
#define MSG_HPP

#include "../commons.hpp"
#include "../fs/inode.hpp"

struct PureValueRequest { int64_t value; };
struct ValueWithPathRequest { int64_t value; int len; char path[MAX_PATH_LEN + 1]; };
struct RawRequest { int len; char raw[4090]; static const size_t RAW_SIZE = 4089; };
struct MemRequest { uintptr_t addr; uint8_t data[2048]; };
union GeneralRequest
{
    PureValueRequest pvReq;
    ValueWithPathRequest vwpReq;
    RawRequest rawReq;
};

struct PureValueResponse { int64_t value; };
struct StatResponse { int result; union { loco_dir_stat dirStat; loco_file_stat fileStat; }; };
struct InodeResponse { int result; union { DirectoryInode di; FileInode fi; }; };
struct RawResponse { int len; char raw[4090]; static const size_t RAW_SIZE = 4089; };
struct MemResponse { uint8_t data[2048]; };
union GeneralResponse
{
    PureValueResponse pvResp;
    StatResponse statResp;
    InodeResponse inodeResp;
    RawResponse rawResp;
};

#endif // MSG_HPP
