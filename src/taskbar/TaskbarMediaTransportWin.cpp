#include "TaskbarComponentWin.h"
#include "input/InputComponent.h"

#include <QScopedValueRollback>
#include <Windows.Foundation.h>
#include <systemmediatransportcontrolsinterop.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Media;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

void TaskbarComponentWin::initializeMediaTransport(HWND hwnd)
{
  if (m_initialized) return;
  ComPtr<ISystemMediaTransportControlsInterop> interop;
  auto hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(), &interop);
  if (FAILED(hr))
  {
    qWarning() << "Failed instantiating interop object";
    return;
  }

  ComPtr<ISystemMediaTransportControls> controls;
  hr = interop->GetForWindow(hwnd, IID_PPV_ARGS(&controls));
  if (FAILED(hr))
  {
    qWarning() << "Failed to GetForWindow";
    return;
  }
  if (m_initialized || m_initializing) return;
  m_systemControls = controls;
  initializeMediaTransportControls();
}

void TaskbarComponentWin::initializeMediaTransportControls()
{
  if (m_initialized || m_initializing) return;
  QScopedValueRollback<bool> initializing(m_initializing, true);
  auto admission = std::make_shared<SmtcAdmission>(this);
  m_admission = admission;
  auto hr = m_systemControls->get_DisplayUpdater(&m_displayUpdater);
  if (FAILED(hr))
  {
    qWarning() << "Failed to get Display updater";
    admission->close();
    return;
  }

  hr = m_systemControls->put_IsEnabled(false);
  hr = m_systemControls->put_IsPlayEnabled(true);
  hr = m_systemControls->put_IsPauseEnabled(true);
  hr = m_systemControls->put_IsPreviousEnabled(true);
  hr = m_systemControls->put_IsNextEnabled(true);
  hr = m_systemControls->put_IsStopEnabled(true);
  hr = m_systemControls->put_IsRewindEnabled(true);
  hr = m_systemControls->put_IsFastForwardEnabled(true);
  hr = m_systemControls->put_IsChannelDownEnabled(true);
  hr = m_systemControls->put_IsChannelUpEnabled(true);

  auto handler = Callback<
    ITypedEventHandler<
      SystemMediaTransportControls*,
      SystemMediaTransportControlsButtonPressedEventArgs*>>(
    [admission](ISystemMediaTransportControls*, ISystemMediaTransportControlsButtonPressedEventArgs* args) -> HRESULT {
      SystemMediaTransportControlsButton button;
      const HRESULT result = args->get_Button(&button);
      if (FAILED(result)) return result;
      SmtcAdmission::enqueue(admission, static_cast<int>(button), &TaskbarComponentWin::dispatchButton);
      return S_OK;
    });
  hr = m_systemControls->add_ButtonPressed(handler.Get(), &m_buttonPressedToken);
  if (FAILED(hr))
  {
    qWarning() << "Failed to add callback handler";
    admission->close();
    return;
  }

  m_initialized = true;
  qInfo() << "SystemMediaTransportControls successfully initialized";
}

/////////////////////////////////////////////////////////////////////////////////////////
HRESULT TaskbarComponentWin::buttonPressed(ISystemMediaTransportControls* sender,
  ISystemMediaTransportControlsButtonPressedEventArgs* args)
{
  SystemMediaTransportControlsButton button;
  auto hr = args->get_Button(&button);
  if (FAILED(hr))
  {
    qWarning() << "Failed to get the pressed button";
    return hr;
  }
  dispatchButton(this, static_cast<int>(button));
  return S_OK;
}

void TaskbarComponentWin::dispatchButton(QObject*, int value)
{
  const auto button = static_cast<SystemMediaTransportControlsButton>(value);
  switch (button)
  {
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Play:
      InputComponent::Get().sendAction("play_pause");
      qDebug() << "Received play button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Pause:
      InputComponent::Get().sendAction("play_pause");
      qDebug() << "Received pause button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Next:
      InputComponent::Get().sendAction("next");
      qDebug() << "Received next button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Previous:
      InputComponent::Get().sendAction("previous");
      qDebug() << "Received previous button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Stop:
      InputComponent::Get().sendAction("stop");
      qDebug() << "Received stop button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_FastForward:
      InputComponent::Get().sendAction("seek_forward");
      qDebug() << "Received seek_forward button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Rewind:
      InputComponent::Get().sendAction("seek_backward");
      qDebug() << "Received seek_backward button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_ChannelUp:
      InputComponent::Get().sendAction("channelup");
      qDebug() << "Received channelup button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_ChannelDown:
      InputComponent::Get().sendAction("channeldown");
      qDebug() << "Received channeldown button press";
      break;
    case SystemMediaTransportControlsButton::SystemMediaTransportControlsButton_Record:
      qDebug() << "Received unsupported button press";
      break;
  }

}
