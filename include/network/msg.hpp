#if !defined(MSG_HPP)
#define MSG_HPP

#include "../commons.hpp"
#include "../fs/inode.hpp"

struct PureValueRequest { int value; } ;
struct ValueWithPathRequest { uint64_t value; int len; char path[MAX_PATH_LEN]; } ;
struct RawRequest { int len; char raw[4090]; } ;
union GeneralRequest
{
    PureValueRequest pvReq;
    ValueWithPathRequest vwpReq;
    RawRequest rawReq;
} ;

struct PureValueResponse { int value; } ;
union StatResponse { loco_dir_stat dirStat; loco_file_stat fileStat; } ;
union InodeResponse { DirectoryInode di; FileInode fi; } ;
struct RawResponse { int len; char raw[4090]; } ;
union GeneralResponse
{
    PureValueResponse pvResp;
    StatResponse statResp;
    InodeResponse inodeResp;
    RawResponse rawResp;
} ;

#endif // MSG_HPP
