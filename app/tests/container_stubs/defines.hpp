#pragma once
#include <switch.h>
#define R_SUCCEED() return 0
#define R_TRY(expr) do {const Result rc_=(expr); if(rc_) return rc_;} while(false)
#define R_UNLESS(expr,code) do {if(!(expr)) return (code);} while(false)
enum : Result {Result_NspInvalidHeader=1,Result_NspBadMagic,Result_XciInvalidHeader,
    Result_XciBadMagic,Result_XciSecurePartitionNotFound,Result_YatiInvalidNcaReadSize};
