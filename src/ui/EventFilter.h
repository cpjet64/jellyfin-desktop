//
// Created by Tobias Hieta on 07/03/16.
//

#ifndef PLEXMEDIAPLAYER_EVENTFILTER_H
#define PLEXMEDIAPLAYER_EVENTFILTER_H

#include <QObject>
#include <QEvent>
class QQuickWindow;

class EventFilter : public QObject
{
  Q_OBJECT
public:
  explicit EventFilter(QObject* parent = nullptr) : QObject(parent), m_currentKeyDown(false) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  bool desktopEventFilter(QObject* watched, QEvent* event, QQuickWindow* window);
  bool m_currentKeyDown;
};

#endif //PLEXMEDIAPLAYER_EVENTFILTER_H
