#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include <MpvController>
#include <QDebug>

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
#ifdef MPVQT_EXTERNAL_EVENTS
    : MpvAbstractItem(MpvController::EventMode::External, parent)
#else
    : MpvAbstractItem(parent)
#endif
{
    connect(this, &MpvAbstractItem::ready, this, [this]() {
        qDebug() << "MpvVideoItem ready() signal fired!";
        m_ready = true;
        initialize();
    });

    qDebug() << "MpvVideoItem constructed";
    // Critical: Set vo=libmpv for Qt integration
    Q_EMIT setProperty("vo", "libmpv");

#ifdef Q_OS_WIN32
    // Force desktop OpenGL and disable advanced features for compatibility with older GPUs
    Q_EMIT setProperty("gpu-api", "opengl");
    Q_EMIT setProperty("opengl-es", "no");
#endif
}

MpvVideoItem::~MpvVideoItem()
{
#ifdef MPVQT_EXTERNAL_EVENTS
    if (m_initialized && m_player)
        m_player->releaseMpvController(mpvController());
#endif
}

void MpvVideoItem::setPlayerComponent(PlayerComponent* player)
{
#ifdef MPVQT_EXTERNAL_EVENTS
    if (m_initialized && m_player != player) {
        qWarning() << "MpvVideoItem cannot change its initialized player owner";
        return;
    }
#endif
    qDebug() << "MpvVideoItem::setPlayerComponent called, mpvController():" << mpvController();
    m_player = player;
    initialize();
}

void MpvVideoItem::initialize() {
    if (m_initialized) {
        qDebug() << "MpvVideoItem is already initialized, ignoring";
        return;
    }

#ifndef MPVQT_EXTERNAL_EVENTS
    if (!m_ready) {
        qDebug() << "MpvVideoItem not ready yet, waiting for ready() signal";
        return;
    }
#endif

    if (!m_player) {
        qDebug() << "MpvVideoItem has no player, waiting for setPlayerComponent()";
        return;
    }

#ifdef MPVQT_EXTERNAL_EVENTS
    if (!m_player->tryBindMpvController(mpvController())) {
        qWarning() << "MpvVideoItem cannot bind a second player controller";
        return;
    }
#else
    m_player->setMpvController(mpvController());
#endif
    m_initialized = true;
    qDebug() << "MpvVideoItem ready, initializing now";
    m_player->initializeMpv();
}
