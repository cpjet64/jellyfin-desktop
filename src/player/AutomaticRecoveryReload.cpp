#include "AutomaticRecoveryReload.h"
#include <QByteArray>
#include <QUrl>
#include <QVector>

namespace {
QByteArray target(std::int64_t positionMs)
{
  return QByteArray::number(positionMs / 1000) + '.' + QByteArray::number(positionMs % 1000).rightJustified(3, '0');
}
}
bool AutomaticRecoveryReload::submit(const AutomaticRecoverySession::Snapshot& snapshot, std::uint64_t userdata,
                                     const Dispatch& dispatch)
{
  if (!dispatch || !userdata || snapshot.url.isEmpty() || snapshot.positionMs < 0 || snapshot.entry <= 0 ||
      !snapshot.key.session || !snapshot.key.revision || !snapshot.key.generation) return false;
  QVector<QByteArray> keys{"start", "hr-seek", "pause", "aid", "sid", "ad", "vd"};
  QVector<QByteArray> values{target(snapshot.positionMs), "yes", "yes", "no", "no", "", ""};
  const auto agent = snapshot.metadata.value("headers").toMap().value("User-Agent").toString();
  if (!agent.isEmpty()) { keys.push_back("user-agent"); values.push_back(agent.toUtf8()); }
  QVector<char*> keyPointers;
  QVector<mpv_node> optionValues;
  keyPointers.reserve(keys.size()); optionValues.resize(values.size());
  for (int i = 0; i < keys.size(); ++i) {
    keyPointers.push_back(keys[i].data());
    optionValues[i].format = MPV_FORMAT_STRING;
    optionValues[i].u.string = values[i].data();
  }
  mpv_node_list options{};
  options.num = keys.size(); options.keys = keyPointers.data(); options.values = optionValues.data();
  QByteArray name("loadfile"), url = QUrl(snapshot.url).toString(QUrl::FullyEncoded).toUtf8(), action("replace");
  mpv_node args[5]{};
  args[0].format = args[1].format = args[2].format = MPV_FORMAT_STRING;
  args[0].u.string = name.data(); args[1].u.string = url.data(); args[2].u.string = action.data();
  args[3].format = MPV_FORMAT_INT64; args[3].u.int64 = -1;
  args[4].format = MPV_FORMAT_NODE_MAP; args[4].u.list = &options;
  mpv_node_list list{}; list.num = 5; list.values = args;
  mpv_node command{}; command.format = MPV_FORMAT_NODE_ARRAY; command.u.list = &list;
  try { return dispatch(userdata, &command) >= 0; } catch (...) { return false; }
}
