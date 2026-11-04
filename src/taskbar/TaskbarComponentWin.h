#ifndef TASKBARCOMPONENTWIN_H
#define TASKBARCOMPONENTWIN_H

#include "QWinTaskbarStubs.h"

#include <wrl.h>
#include <Windows.Media.h>
#include <memory>

#include "TaskbarComponent.h"
#include "SmtcAdmission.h"

class TaskbarComponentWin : public TaskbarComponent
{
public:
  TaskbarComponentWin(): TaskbarComponent(nullptr) {}
  ~TaskbarComponentWin() override;
  virtual void setWindow(QQuickWindow* window) override;

private:
  friend class TestWindowsNativeControls;
  friend class TestWindowsSmtcLifetime;
  void onPauseClicked();
  void onPrevClicked();
  void onNextClicked();
  void setProgress(quint64 value);
  void setControlsVisible(bool value);
  void setPaused(bool value);
  void initializeMediaTransport(HWND hwnd);
  void initializeMediaTransportControls();
  void shutdownMediaTransportControls();
  void onMetaData(const QVariantMap &meta, QUrl baseUrl);
  void setAudioMeta(const QVariantMap &meta);
  void setVideoMeta(const QVariantMap &meta);
  void setThumbnail(const QVariantMap &meta, QUrl baseUrl);
  void playing();
  void stopped();
  void paused();

  HRESULT buttonPressed(ABI::Windows::Media::ISystemMediaTransportControls* sender,
    ABI::Windows::Media::ISystemMediaTransportControlsButtonPressedEventArgs* args);
  static void dispatchButton(QObject* receiver, int button);

  QWinTaskbarButton* m_button;
  QWinThumbnailToolBar* m_toolbar;
  QWinThumbnailToolButton* m_pause;
  QWinThumbnailToolButton* m_prev;
  QWinThumbnailToolButton* m_next;

  bool m_initialized = false;
  bool m_initializing = false;
  std::shared_ptr<SmtcAdmission> m_admission;
  EventRegistrationToken m_buttonPressedToken;
  Microsoft::WRL::ComPtr<ABI::Windows::Media::ISystemMediaTransportControls> m_systemControls;
  Microsoft::WRL::ComPtr<ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater> m_displayUpdater;
  Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReference> m_thumbnail;
};

#endif // TASKBARCOMPONENTWIN_H
