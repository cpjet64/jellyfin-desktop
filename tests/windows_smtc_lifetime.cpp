#include <QtTest/QtTest>
#include <wrl/implements.h>
#include "taskbar/TaskbarComponentWin.h"

using namespace ABI::Windows::Media;
using namespace Microsoft::WRL;

class FakeUpdater final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISystemMediaTransportControlsDisplayUpdater>
{
  InspectableClass(L"Jellyfin.Test.Updater", BaseTrust)
public:
  int clears = 0;
  IFACEMETHODIMP get_Type(MediaPlaybackType*) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_Type(MediaPlaybackType) override { return E_NOTIMPL; }
  IFACEMETHODIMP get_AppMediaId(HSTRING*) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_AppMediaId(HSTRING) override { return E_NOTIMPL; }
  IFACEMETHODIMP get_Thumbnail(ABI::Windows::Storage::Streams::IRandomAccessStreamReference**) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_Thumbnail(ABI::Windows::Storage::Streams::IRandomAccessStreamReference*) override { return E_NOTIMPL; }
  IFACEMETHODIMP get_MusicProperties(IMusicDisplayProperties**) override { return E_NOTIMPL; }
  IFACEMETHODIMP get_VideoProperties(IVideoDisplayProperties**) override { return E_NOTIMPL; }
  IFACEMETHODIMP get_ImageProperties(IImageDisplayProperties**) override { return E_NOTIMPL; }
  IFACEMETHODIMP CopyFromFileAsync(MediaPlaybackType, ABI::Windows::Storage::IStorageFile*, __FIAsyncOperation_1_boolean**) override { return E_NOTIMPL; }
  IFACEMETHODIMP ClearAll() override { ++clears; return S_OK; }
  IFACEMETHODIMP Update() override { return E_NOTIMPL; }
};

class FakeControls final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISystemMediaTransportControls>
{
  InspectableClass(L"Jellyfin.Test.Controls", BaseTrust)
public:
  ComPtr<FakeUpdater> updater = Make<FakeUpdater>();
  HRESULT updaterResult = S_OK;
  HRESULT addResult = S_OK;
  int adds = 0, removes = 0, gets = 0;
  EventRegistrationToken removed{};
  IFACEMETHODIMP get_PlaybackStatus(MediaPlaybackStatus*) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_PlaybackStatus(MediaPlaybackStatus) override { return S_OK; }
  IFACEMETHODIMP get_DisplayUpdater(ISystemMediaTransportControlsDisplayUpdater** value) override
  {
    ++gets;
    if (FAILED(updaterResult)) return updaterResult;
    return updater.CopyTo(value);
  }
  IFACEMETHODIMP get_SoundLevel(SoundLevel*) override { return E_NOTIMPL; }
#define FAKE_ENABLED(Name) \
  IFACEMETHODIMP get_Is##Name##Enabled(boolean*) override { return E_NOTIMPL; } \
  IFACEMETHODIMP put_Is##Name##Enabled(boolean) override { return S_OK; }
  IFACEMETHODIMP get_IsEnabled(boolean*) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_IsEnabled(boolean) override { return S_OK; }
  FAKE_ENABLED(Play)
  FAKE_ENABLED(Stop)
  FAKE_ENABLED(Pause)
  FAKE_ENABLED(Record)
  FAKE_ENABLED(FastForward)
  FAKE_ENABLED(Rewind)
  FAKE_ENABLED(Previous)
  FAKE_ENABLED(Next)
  FAKE_ENABLED(ChannelUp)
  FAKE_ENABLED(ChannelDown)
#undef FAKE_ENABLED
  IFACEMETHODIMP add_ButtonPressed(__FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs*, EventRegistrationToken* token) override
  {
    ++adds;
    if (FAILED(addResult)) return addResult;
    token->value = 4812;
    return S_OK;
  }
  IFACEMETHODIMP remove_ButtonPressed(EventRegistrationToken token) override
  { ++removes; removed = token; return S_OK; }
  IFACEMETHODIMP add_PropertyChanged(__FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsPropertyChangedEventArgs*, EventRegistrationToken*) override { return E_NOTIMPL; }
  IFACEMETHODIMP remove_PropertyChanged(EventRegistrationToken) override { return E_NOTIMPL; }
};

class TestWindowsSmtcLifetime : public QObject
{
  Q_OBJECT
private slots:
  void updaterFailure();
  void registrationFailure();
  void registeredLifetime();
};

void TestWindowsSmtcLifetime::updaterFailure()
{
  auto controls = Make<FakeControls>();
  controls->updaterResult = E_FAIL;
  {
    TaskbarComponentWin taskbar;
    taskbar.m_systemControls = controls;
    taskbar.initializeMediaTransportControls();
    QVERIFY(!taskbar.m_initialized);
  }
  QCOMPARE(controls->gets, 1);
  QCOMPARE(controls->adds, 0);
  QCOMPARE(controls->removes, 0);
  QCOMPARE(controls->updater->clears, 0);
}

void TestWindowsSmtcLifetime::registrationFailure()
{
  auto controls = Make<FakeControls>();
  controls->addResult = E_FAIL;
  {
    TaskbarComponentWin taskbar;
    taskbar.m_systemControls = controls;
    taskbar.initializeMediaTransportControls();
    QVERIFY(!taskbar.m_initialized);
  }
  QCOMPARE(controls->gets, 1);
  QCOMPARE(controls->adds, 1);
  QCOMPARE(controls->removes, 0);
  QCOMPARE(controls->updater->clears, 0);
}

void TestWindowsSmtcLifetime::registeredLifetime()
{
  auto controls = Make<FakeControls>();
  {
    TaskbarComponentWin taskbar;
    taskbar.m_systemControls = controls;
    taskbar.initializeMediaTransportControls();
    QVERIFY(taskbar.m_initialized);
    QCOMPARE(controls->gets, 1);
    QCOMPARE(controls->adds, 1);
    QCOMPARE(controls->removes, 0);
  }
  QCOMPARE(controls->removes, 1);
  QCOMPARE(controls->removed.value, 4812LL);
  QCOMPARE(controls->updater->clears, 1);
}

QTEST_GUILESS_MAIN(TestWindowsSmtcLifetime)
#include "windows_smtc_lifetime.moc"
