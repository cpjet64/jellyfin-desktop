#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <cstdio>

// GUI-thread only. Each sample is supplied by an already evaluated predicate;
// no callback here reads mpv, drains an observer, or pumps the event loop.
class C2QuiescenceObservation
{
public:
  void sample(const QString& phase, const QJsonObject& value)
  {
    if (phase.size() > 128 || (!phases.contains(phase) && phases.size() >= 64)) { truncated = true; return; }
    auto item = phases.value(phase).toObject();
    const int count = item.value("count").toInt();
    const QJsonObject safe = bounded(value);
    if (!count) item.insert("first", safe);
    item.insert("last", safe);
    item.insert("count", count < 1000000000 ? count + 1 : count);
    phases.insert(phase, item);
  }
  bool check(const QString& phase, const std::function<bool()>& predicate)
  {
    const bool result = predicate();
    sample(phase, {{"result", result}});
    if (!result) { stage = phase; reason = "immediate-rejection"; }
    return result;
  }
  bool wait(const QString& phase, const std::function<bool(const std::function<bool()>&)>& run,
            const std::function<bool()>& predicate)
  {
    const bool result = run([&] {
      const bool value = predicate();
      sample(phase, {{"result", value}});
      return value;
    });
    sample(phase + "-outcome", {{"completed", result}, {"reason", result ? "success" : "timeout"}});
    if (!result) { stage = phase; reason = "wait-timeout"; }
    return result;
  }
  void value(const QString& key, const QJsonValue& value)
  {
    if (key.size() > 128 || (!values.contains(key) && values.size() >= 64)) { truncated = true; return; }
    const QJsonObject safe = bounded({{"value", value}});
    values.insert(key, safe.value("value").isUndefined() ? QJsonValue("truncated") : safe.value("value"));
  }
  QJsonObject record(bool original) const
  {
    return {{"sampling", "sequential-evaluated-values"}, {"phases", phases}, {"values", values},
      {"original_verdict", original}, {"exit_stage", stage}, {"exit_reason", reason}, {"truncated", truncated},
      {"evidence_write", "attempted; record presence requires successful write and flush"}};
  }
  bool finish(bool original, const std::function<bool(const QJsonObject&)>& writer,
              const std::function<void(const QString&, const QString&)>& fallback = reportWriteFailure)
  {
    if (original) { stage = "complete"; reason = "success"; }
    const bool written = writer(record(original));
    if (!written) fallback(stage, reason);
    return original && written;
  }
private:
  static void reportWriteFailure(const QString& phase, const QString& reason)
  {
    const auto p = phase.left(128).toUtf8();
    const auto r = reason.left(128).toUtf8();
    std::fprintf(stderr, "C2 quiescence evidence write failed; original stage=%s reason=%s\n", p.constData(), r.constData());
    std::fflush(stderr);
  }
  QJsonObject bounded(const QJsonObject& value)
  {
    if (QJsonDocument(value).toJson(QJsonDocument::Compact).size() <= 16384) return value;
    truncated = true;
    return {{"truncated", true}};
  }
  QJsonObject phases, values;
  QString stage = "not-entered", reason = "not-completed";
  bool truncated = false;
};
