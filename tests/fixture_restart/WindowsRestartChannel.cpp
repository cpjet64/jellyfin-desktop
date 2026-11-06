#include "WindowsRestartChannel.h"
#ifdef _WIN32
#include <cstddef>
#include <string>
#include <vector>

namespace FixtureRestart {
WindowsRestartChannel* WindowsRestartChannel::create(const WindowsChannelApi& api)
{ return new WindowsRestartChannel(api); }

WindowsRestartChannel::~WindowsRestartChannel()
{
  if (child_ != INVALID_HANDLE_VALUE) api_.close(child_);
  if (server_ != INVALID_HANDLE_VALUE) api_.close(server_);
  if (connection_.hEvent) api_.close(connection_.hEvent);
}

bool WindowsRestartChannel::release()
{
  if (pending_ || owned_) return false;
  delete this;
  return true;
}

PSECURITY_DESCRIPTOR WindowsRestartChannel::security()
{
  HANDLE token = nullptr;
  if (!api_.openToken(api_.currentProcess(), TOKEN_QUERY, &token)) return nullptr;
  DWORD size = 0;
  api_.tokenInfo(token, TokenUser, nullptr, 0, &size);
  std::vector<std::max_align_t> data((size + sizeof(std::max_align_t) - 1) /
                                    sizeof(std::max_align_t));
  const bool valid = size && api_.tokenInfo(token, TokenUser, data.data(), size, &size);
  api_.close(token);
  if (!valid) return nullptr;
  LPWSTR sid = nullptr;
  if (!api_.sidString(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) return nullptr;
  const std::wstring acl = std::wstring(L"D:P(A;;GA;;;") + sid + L")(A;;GA;;;SY)";
  api_.localFree(sid);
  PSECURITY_DESCRIPTOR result = nullptr;
  if (!api_.descriptor(acl.c_str(), SDDL_REVISION_1, &result, nullptr)) return nullptr;
  return result;
}

ChannelProgress WindowsRestartChannel::begin(ULONGLONG deadline)
{
  if (started_ || api_.now() >= deadline) return ChannelProgress::Failed;
  started_ = true;
  deadline_ = deadline;
  unsigned char random[16]{};
  if (api_.random(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    return state_ = ChannelProgress::Failed;
  constexpr wchar_t hex[] = L"0123456789abcdef";
  std::wstring name = L"\\\\.\\pipe\\jellyfin-restart-";
  for (auto byte : random) { name += hex[byte >> 4]; name += hex[byte & 15]; }
  PSECURITY_DESCRIPTOR descriptor = security();
  if (!descriptor) return state_ = ChannelProgress::Failed;
  SECURITY_ATTRIBUTES parentSecurity{sizeof(parentSecurity), descriptor, FALSE};
  server_ = api_.createPipe(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
      FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
      PIPE_REJECT_REMOTE_CLIENTS, 1, 16384, 16384, 0, &parentSecurity);
  api_.localFree(descriptor);
  if (server_ == INVALID_HANDLE_VALUE) return state_ = ChannelProgress::Failed;
  connection_.hEvent = api_.event(nullptr, TRUE, FALSE, nullptr);
  if (!connection_.hEvent || api_.now() >= deadline_) return state_ = ChannelProgress::Failed;
  if (api_.connect(server_, &connection_)) return state_ = ChannelProgress::Failed;
  if (api_.error() != ERROR_IO_PENDING) return state_ = ChannelProgress::Failed;
  pending_ = true;
  SECURITY_ATTRIBUTES childSecurity{sizeof(childSecurity), nullptr, TRUE};
  child_ = api_.createFile(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &childSecurity,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (child_ == INVALID_HANDLE_VALUE || api_.now() >= deadline_) return cancel();
  return poll();
}

ChannelProgress WindowsRestartChannel::poll()
{
  if (!pending_) return state_;
  DWORD bytes = 0;
  if (api_.result(server_, &connection_, &bytes, FALSE)) {
    pending_ = false;
    if (cancelling_ || api_.now() >= deadline_ || child_ == INVALID_HANDLE_VALUE)
      return state_ = ChannelProgress::Failed;
    connected_ = true;
    return state_ = ChannelProgress::Complete;
  }
  const DWORD error = api_.error();
  if (error == ERROR_IO_INCOMPLETE) {
    if (api_.now() >= deadline_ && !cancelling_) return cancel();
    return cancelling_ ? ChannelProgress::Unconfirmed : ChannelProgress::Pending;
  }
  if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA)
    return ChannelProgress::Unconfirmed;
  pending_ = false;
  return state_ = ChannelProgress::Failed;
}

ChannelProgress WindowsRestartChannel::cancel()
{
  if (!pending_) return state_;
  if (!cancelling_) {
    cancelling_ = true;
    if (!api_.cancel(server_, &connection_) && api_.error() != ERROR_NOT_FOUND)
      return ChannelProgress::Unconfirmed;
  }
  return poll();
}
}
#endif
