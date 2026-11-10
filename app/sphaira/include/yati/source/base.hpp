#pragma once

#include <vector>
#include <switch.h>
#include "defines.hpp"
#include "read_exact.hpp"

namespace sphaira::yati::source {

struct Base {
    virtual ~Base() = default;
    // virtual Result Read(void* buf, s64 off, s64 size, u64* bytes_read) = 0;
    virtual Result Read(void* buf, s64 off, s64 size, u64* bytes_read) = 0;

    Result Read2(void* buf, s64 off, s64 size) {
        return ReadExact(*this, buf, off, size, Result_YatiInvalidNcaReadSize);
    }

    virtual bool IsStream() const {
        return false;
    }

    virtual void SignalCancel() {

    }

    Result GetOpenResult() const {
        return m_open_result;
    }

protected:
    Result m_open_result{};
};

} // namespace sphaira::yati::source
