#include <QtTest/QtTest>
#include <wrl/implements.h>
#include <wrl/event.h>
#include <condition_variable>
#include <functional>
#include <thread>
#include "smtc_admission_model.h"
#include "taskbar/TaskbarComponentWin.h"

using namespace ABI::Windows::Media;
using namespace Microsoft::WRL;
using ButtonHandler = __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs;

class ModelButtonArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISystemMediaTransportControlsButtonPressedEventArgs>
{
  InspectableClass(L"Jellyfin.Test.ModelButtonArgs", BaseTrust)
public:
  SystemMediaTransportControlsButton button = SystemMediaTransportControlsButton_Play;
  IFACEMETHODIMP get_Button(SystemMediaTransportControlsButton* out) override
  { if (!out) return E_POINTER; *out = button; return S_OK; }
};

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
  HRESULT removeResult = S_OK;
  ComPtr<ButtonHandler> retained;
  std::function<void()> duringRemove;
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
  IFACEMETHODIMP add_ButtonPressed(ButtonHandler* handler, EventRegistrationToken* token) override
  {
    ++adds;
    if (FAILED(addResult)) return addResult;
    retained = handler;
    token->value = 4812;
    return S_OK;
  }
  IFACEMETHODIMP remove_ButtonPressed(EventRegistrationToken token) override
  { ++removes; removed = token; if (duringRemove) duringRemove(); if (SUCCEEDED(removeResult)) retained.Reset(); return removeResult; }
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
  void failedRemovalRetainsProductionDelegate();
  void modelClosedAndReentrant();
  void modelHeldAndNewRegistration();
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

void TestWindowsSmtcLifetime::failedRemovalRetainsProductionDelegate()
{
  auto controls = Make<FakeControls>();
  controls->removeResult = E_FAIL;
  {
    TaskbarComponentWin taskbar;
    taskbar.m_systemControls = controls;
    taskbar.initializeMediaTransportControls();
    QVERIFY(controls->retained);
  }
  QCOMPARE(controls->removes, 1);
  QVERIFY(controls->retained); // Never invoke the real raw-owner callback here.
  controls->retained.Reset();
}

static ComPtr<ButtonHandler> makeModelHandler(const std::shared_ptr<SmtcAdmissionModel>& state,
                                              std::function<void()> beforeEnqueue = {})
{
  return Callback<ButtonHandler>([state, beforeEnqueue](ISystemMediaTransportControls*,
                                        ISystemMediaTransportControlsButtonPressedEventArgs* args) -> HRESULT {
    SystemMediaTransportControlsButton button;
    const HRESULT hr = args->get_Button(&button);
    if (FAILED(hr)) return hr;
    if (beforeEnqueue) beforeEnqueue();
    smtcModelEnqueue(state, static_cast<int>(button));
    return S_OK;
  });
}

void TestWindowsSmtcLifetime::modelClosedAndReentrant()
{
  std::vector<int> values;
  auto receiver = std::make_unique<SmtcModelReceiver>(values);
  auto state = std::make_shared<SmtcAdmissionModel>(receiver.get());
  auto controls = Make<FakeControls>();
  auto args = Make<ModelButtonArgs>();
  auto handler = makeModelHandler(state);
  EventRegistrationToken token{};
  QCOMPARE(controls->add_ButtonPressed(handler.Get(), &token), S_OK);
  QCOMPARE(controls->retained->Invoke(controls.Get(), args.Get()), S_OK);
  smtcModelClose(state);
  smtcModelClose(state);
  controls->removeResult = E_FAIL;
  controls->duringRemove = [&] { QCOMPARE(controls->retained->Invoke(controls.Get(), args.Get()), S_OK); };
  QCOMPARE(controls->remove_ButtonPressed(token), E_FAIL);
  receiver.reset();
  QCOMPARE(controls->retained->Invoke(controls.Get(), args.Get()), S_OK);
  QCoreApplication::processEvents();
  QVERIFY(values.empty());
}

void TestWindowsSmtcLifetime::modelHeldAndNewRegistration()
{
  std::vector<int> oldValues, newValues;
  auto oldReceiver = std::make_unique<SmtcModelReceiver>(oldValues);
  auto oldState = std::make_shared<SmtcAdmissionModel>(oldReceiver.get());
  std::weak_ptr<SmtcAdmissionModel> oldWeak = oldState;
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false;
  auto oldHandler = makeModelHandler(oldState, [&] {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_one();
    cv.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
  });
  auto controls = Make<FakeControls>();
  EventRegistrationToken token{};
  QCOMPARE(controls->add_ButtonPressed(oldHandler.Get(), &token), S_OK);
  ComPtr<ButtonHandler> retainedOld = controls->retained;
  auto args = Make<ModelButtonArgs>();
  std::thread worker([&] { retainedOld->Invoke(nullptr, args.Get()); });
  bool observed = false;
  { std::unique_lock<std::mutex> lock(mutex); observed = cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }); }
  smtcModelClose(oldState);
  oldReceiver.reset();
  SmtcModelReceiver newReceiver(newValues);
  auto newState = std::make_shared<SmtcAdmissionModel>(&newReceiver);
  auto newHandler = makeModelHandler(newState);
  const HRESULT addNewResult = controls->add_ButtonPressed(newHandler.Get(), &token);
  { std::lock_guard<std::mutex> lock(mutex); release = true; }
  cv.notify_one();
  worker.join();
  QVERIFY(observed);
  QCOMPARE(addNewResult, S_OK);
  QCOMPARE(controls->retained->Invoke(nullptr, args.Get()), S_OK);
  QCoreApplication::processEvents();
  QVERIFY(oldValues.empty());
  QCOMPARE(newValues.size(), size_t(1));
  QCOMPARE(newValues.front(), static_cast<int>(SystemMediaTransportControlsButton_Play));
  smtcModelClose(newState);
  oldHandler.Reset();
  retainedOld.Reset();
  oldState.reset();
  QVERIFY(oldWeak.expired());
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
