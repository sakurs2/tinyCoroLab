#pragma once

#include <coroutine>
#include <cstdint>

namespace coro
{
  using std::coroutine_handle;
  enum Tasktype
  {
    Accept,
    TcpRead,
    TcpWrite,
    None
  };

  struct IoInfo
  {
    coroutine_handle<> handle;
    int32_t result;
    Tasktype type;
    uintptr_t data;
  };

  inline uintptr_t ioinfo_to_ptr(IoInfo *info) noexcept
  {
    return reinterpret_cast<uintptr_t>(info);
  }

  inline IoInfo *ptr_to_ioinfo(uintptr_t ptr) noexcept
  {
    return reinterpret_cast<IoInfo *>(ptr);
  }
};