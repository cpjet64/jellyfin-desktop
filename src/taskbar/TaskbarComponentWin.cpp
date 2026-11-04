#include <QApplication>
#include <QStyle>
#include <QUrlQuery>

#include <Windows.Foundation.h>
#include <systemmediatransportcontrolsinterop.h>
#include <wrl\client.h>
#include <wrl\wrappers\corewrappers.h>

#include "TaskbarComponentWin.h"
#include "PlayerComponent.h"
#include "input/InputComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Media;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using ABI::Windows::Storage::Streams::IRandomAccessStreamReference;
using ABI::Windows::Storage::Streams::IRandomAccessStreamReferenceStatics;


TaskbarComponentWin::~TaskbarComponentWin()
{
  shutdownMediaTransportControls();
}

void TaskbarComponentWin::shutdownMediaTransportControls()
{
  if (m_admission) m_admission->close();
  const bool registered = m_initialized;
  m_initialized = false;
  if (registered)
  {
    const HRESULT hr = m_systemControls->remove_ButtonPressed(m_buttonPressedToken);
    if (FAILED(hr)) qWarning() << "Failed to remove media control callback:" << Qt::hex << hr;
    m_displayUpdater->ClearAll();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setWindow(QQuickWindow* window)
{
  qDebug() << "Taskbar initialization started";
  TaskbarComponent::setWindow(window);

  bool EnableTaskbar = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "enableWindowsTaskbarIntegration").toBool();
  bool EnableMediaControls = SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "enableWindowsMediaIntegration").toBool();

  if (EnableTaskbar) {
    m_button = new QWinTaskbarButton(m_window);
    m_button->setWindow(m_window);

    m_toolbar = new QWinThumbnailToolBar(m_window);
    m_toolbar->setWindow(m_window);

    m_prev = new QWinThumbnailToolButton(m_toolbar);
    connect(m_prev, &QWinThumbnailToolButton::clicked, this, &TaskbarComponentWin::onPrevClicked);

    m_pause = new QWinThumbnailToolButton(m_toolbar);
    connect(m_pause, &QWinThumbnailToolButton::clicked, this, &TaskbarComponentWin::onPauseClicked);

    m_next = new QWinThumbnailToolButton(m_toolbar);
    connect(m_next, &QWinThumbnailToolButton::clicked, this, &TaskbarComponentWin::onNextClicked);

    m_prev->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaSkipBackward));
    m_next->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaSkipForward));

    m_toolbar->addButton(m_prev);
    m_toolbar->addButton(m_pause);
    m_toolbar->addButton(m_next);
  }

  connect(&PlayerComponent::Get(), &PlayerComponent::positionUpdate, this, &TaskbarComponentWin::setProgress);
  connect(&PlayerComponent::Get(), &PlayerComponent::playing, this, &TaskbarComponentWin::playing);
  connect(&PlayerComponent::Get(), &PlayerComponent::paused, this, &TaskbarComponentWin::paused);
  connect(&PlayerComponent::Get(), &PlayerComponent::stopped, this, &TaskbarComponentWin::stopped);
  connect(&PlayerComponent::Get(), &PlayerComponent::onMetaData, this, &TaskbarComponentWin::onMetaData);

  setControlsVisible(false);
  setPaused(false);

  if (EnableMediaControls) {
    initializeMediaTransport((HWND)window->winId());
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onPauseClicked()
{
  InputComponent::Get().sendAction("play_pause");
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onNextClicked()
{
  InputComponent::Get().sendAction("next");
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onPrevClicked()
{
  InputComponent::Get().sendAction("previous");
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::playing()
{
  setControlsVisible(true);
  setPaused(false);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::paused()
{
  setPaused(true);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::stopped()
{
  setControlsVisible(false);
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setControlsVisible(bool value)
{
  if (m_button) {
    m_button->progress()->setVisible(value);

    for (auto& button : m_toolbar->buttons())
    {
      button->setVisible(value);
    }
  }

  if (m_initialized)
  {
    m_systemControls->put_PlaybackStatus(MediaPlaybackStatus::MediaPlaybackStatus_Stopped);
    m_systemControls->put_IsEnabled(value);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setProgress(quint64 value)
{
  if (m_button) {
    qint64 duration = PlayerComponent::Get().getDuration();
    int progress = 0;
    if (duration != 0) {
      progress = (int) (value / duration / 10);
    }
    m_button->progress()->setValue(progress);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setPaused(bool value)
{
  if (m_button) {
    if (value)
    {
      // m_pause->setToolTip("Resume");
      m_pause->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
    }
    else
    {
      // m_pause->setToolTip("Pause");
      m_pause->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPause));
    }

    m_button->progress()->setPaused(value);
  }

  if (m_initialized)
  {
    auto status = value ? MediaPlaybackStatus::MediaPlaybackStatus_Paused : MediaPlaybackStatus::MediaPlaybackStatus_Playing;
    m_systemControls->put_PlaybackStatus(status);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::onMetaData(const QVariantMap& meta, QUrl baseUrl)
{
  if (!m_initialized)
    return;

  HRESULT hr;
  auto mediaType = meta["MediaType"].toString();

  hr = m_displayUpdater->ClearAll();
  if (FAILED(hr))
  {
    qWarning() << "Failed to clear display metadata";
    return;
  }

  if (mediaType == "Video")
  {
    setVideoMeta(meta);
  }
  else // if (mediaType == "Audio") most likely
  {
    setAudioMeta(meta);
  }

  setThumbnail(meta, baseUrl);

  hr = m_displayUpdater->Update();
  if (FAILED(hr))
  {
    qWarning() << "Failed to update the display";
    return;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setAudioMeta(const QVariantMap& meta)
{
  HRESULT hr;

  hr = m_displayUpdater->put_Type(MediaPlaybackType::MediaPlaybackType_Music);
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the media type to music";
    return;
  }

  ComPtr<IMusicDisplayProperties> musicProps;
  hr = m_displayUpdater->get_MusicProperties(musicProps.GetAddressOf());
  if (FAILED(hr))
  {
    qWarning() << "Failed to get music properties";
    return;
  }

  auto artist = meta["Artists"].toStringList().join(", ");
  hr = musicProps->put_Artist(HStringReference((const wchar_t*)artist.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the music's artist";
    return;
  }

  auto title = meta["Name"].toString();
  hr = musicProps->put_Title(HStringReference((const wchar_t*)title.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the music's title";
    return;
  }

  auto albumArtist = meta["AlbumArtist"].toString();
  hr = musicProps->put_AlbumArtist(HStringReference((const wchar_t*)albumArtist.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the music's album artist";
    return;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setVideoMeta(const QVariantMap& meta)
{
  HRESULT hr;

  hr = m_displayUpdater->put_Type(MediaPlaybackType::MediaPlaybackType_Video);
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the media type to video";
    return;
  }

  ComPtr<IVideoDisplayProperties> videoProps;
  hr = m_displayUpdater->get_VideoProperties(videoProps.GetAddressOf());
  if (FAILED(hr))
  {
    qWarning() << "Failed to get video properties";
    return;
  }

  auto title = meta["Name"].toString();
  hr = videoProps->put_Title(HStringReference((const wchar_t*)title.utf16()).Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set the video title";
    return;
  }

  if (meta["Type"] == "Episode")
  {
    auto subtitle = meta["SeriesName"].toString();
    hr = videoProps->put_Subtitle(HStringReference((const wchar_t*)subtitle.utf16()).Get());
    if (FAILED(hr))
    {
      qWarning() << "Failed to set the video subtitle";
      return;
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void TaskbarComponentWin::setThumbnail(const QVariantMap& meta, QUrl baseUrl)
{
  QString imgUrl;
  HRESULT hr;

  auto images = meta["ImageTags"].toMap();
  if (images.contains("Primary"))
  {
    auto itemId = meta["Id"].toString();
    auto imgTag = images["Primary"].toString();
    QUrlQuery query;
    query.addQueryItem("tag", imgTag);
    baseUrl.setPath("/Items/" + itemId + "/Images/Primary");
    baseUrl.setQuery(query);
    imgUrl = baseUrl.toString();
  }
  else if (meta.contains("AlbumId") && meta.contains("AlbumPrimaryImageTag")
           && meta["AlbumId"].canConvert(QMetaType::QString)
           && meta["AlbumPrimaryImageTag"].canConvert(QMetaType::QString))
  {
    auto itemId = meta["AlbumId"].toString();
    auto imgTag = meta["AlbumPrimaryImageTag"].toString();
    QUrlQuery query;
    query.addQueryItem("tag", imgTag);
    baseUrl.setPath("/Items/" + itemId + "/Images/Primary");
    baseUrl.setQuery(query);
    imgUrl = baseUrl.toString();
  }
  else
  {
    qDebug() << "No Primary image found. Do nothing";
    return;
  }

  

  ComPtr<IRandomAccessStreamReferenceStatics> streamRefFactory;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference).Get(),
    &streamRefFactory);
  if (FAILED(hr))
  {
    qWarning() << "Failed instantiating stream factory";
    return;
  }

  ComPtr<IUriRuntimeClassFactory> uriFactory;
  hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(), &uriFactory);
  if (FAILED(hr))
  {
    qWarning() << "Failed instantiating uri factory";
    return;
  }

  ComPtr<IUriRuntimeClass> uri;
  hr = uriFactory->CreateUri(HStringReference((const wchar_t*)imgUrl.utf16()).Get(), &uri);
  if (FAILED(hr))
  {
    qWarning() << "Failed to create uri";
    return;
  }

  hr = streamRefFactory->CreateFromUri(uri.Get(), &m_thumbnail);
  if (FAILED(hr))
  {
    qWarning() << "Failed to create stream from uri";
    return;
  }

  hr = m_displayUpdater->put_Thumbnail(m_thumbnail.Get());
  if (FAILED(hr))
  {
    qWarning() << "Failed to set thumbnail";
    return;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
