#ifndef MPVVIDEOITEM_H
#define MPVVIDEOITEM_H

#include <MpvAbstractItem>
#ifdef MPVQT_EXTERNAL_EVENTS
#include <QPointer>
#endif

class PlayerComponent;

class MpvVideoItem : public MpvAbstractItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);
    ~MpvVideoItem() override;
    void setPlayerComponent(PlayerComponent* player);

    MpvController* controller() { return mpvController(); }

private:
    void initialize();

#ifdef MPVQT_EXTERNAL_EVENTS
    QPointer<PlayerComponent> m_player;
#else
    PlayerComponent* m_player = nullptr;
#endif
    bool m_initialized = false;
    bool m_ready = false;
};

#endif // MPVVIDEOITEM_H
