#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <wrl/implements.h>
#include <wrl/event.h>
#include <condition_variable>
#include <functional>
#include <thread>
#include <chrono>
#include <memory>
#include "input/InputComponent.h"
#include "system/SystemComponent.h"
#include "taskbar/TaskbarComponentWin.h"

using namespace ABI::Windows::Media;
using namespace Microsoft::WRL;
using ButtonHandler = __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs;

class ModelButtonArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISystemMediaTransportControlsButtonPressedEventArgs>
{
  InspectableClass(L"Jellyfin.Test.ModelButtonArgs", BaseTrust)
public:
  SystemMediaTransportControlsButton button = SystemMediaTransportControlsButton_Play;
  HRESULT result = S_OK;
  IFACEMETHODIMP get_Button(SystemMediaTransportControlsButton* out) override
  { if (!out) return E_POINTER; if (FAILED(result)) return result; *out = button; return S_OK; }
};

struct HeldGetter
{
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool released = false;
  bool timedOut = false;
};

class BlockingButtonArgs final : public RuntimeClass<RuntimeClassFlags<WinRt>, ISystemMediaTransportControlsButtonPressedEventArgs>
{
  InspectableClass(L"Jellyfin.Test.BlockingButtonArgs", BaseTrust)
public:
  std::shared_ptr<HeldGetter> gate;
  IFACEMETHODIMP get_Button(SystemMediaTransportControlsButton* out) override
  {
    if (!out) return E_POINTER;
    std::unique_lock<std::mutex> lock(gate->mutex);
    gate->entered = true;
    gate->changed.notify_all();
    if (!gate->changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate->released; })) {
      gate->timedOut = true;
      return E_ABORT;
    }
    *out = SystemMediaTransportControlsButton_Play;
    return S_OK;
  }
};

struct JoinHeldWorker
{
  std::shared_ptr<HeldGetter> gate;
  std::thread worker;
  ~JoinHeldWorker()
  {
    { std::lock_guard<std::mutex> lock(gate->mutex); gate->released = true; }
    gate->changed.notify_all();
    if (worker.joinable()) worker.join();
  }
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
  void retainedAfterClose();
  void queuedAndReentrantClose();
  void heldCallbackAndReplacement();
  void healthyAndGetterFailure();
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
  SystemComponent::Get().hello("smtc-test");
  QSignalSpy actions(&InputComponent::Get(), &InputComponent::hostInput);
  auto controls = Make<FakeControls>();
  controls->removeResult = E_FAIL;
  {
    TaskbarComponentWin taskbar;
    taskbar.m_systemControls = controls;
    taskbar.initializeMediaTransportControls();
    QVERIFY(controls->retained);
  }
  QCOMPARE(controls->removes, 1);
  QVERIFY(controls->retained);
  auto args = Make<ModelButtonArgs>();
  QCOMPARE(controls->retained->Invoke(nullptr, args.Get()), S_OK);
  QCoreApplication::processEvents();
  QCOMPARE(actions.count(), 0);
  controls->retained.Reset();
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

void TestWindowsSmtcLifetime::retainedAfterClose()
{
  SystemComponent::Get().hello("smtc-test");
  QSignalSpy actions(&InputComponent::Get(), &InputComponent::hostInput);
  auto args = Make<ModelButtonArgs>();
  for (const HRESULT removal : {S_OK, E_FAIL}) {
    auto controls = Make<FakeControls>();
    controls->removeResult = removal;
    ComPtr<ButtonHandler> snapshot;
    std::weak_ptr<SmtcAdmission> weak;
    {
      TaskbarComponentWin taskbar;
      taskbar.m_systemControls = controls;
      taskbar.initializeMediaTransportControls();
      QVERIFY(taskbar.m_initialized);
      snapshot = controls->retained;
      weak = taskbar.m_admission;
      taskbar.shutdownMediaTransportControls();
      taskbar.shutdownMediaTransportControls();
      QCOMPARE(controls->removes, 1);
      QCOMPARE(controls->removed.value, 4812LL);
    }
    QCOMPARE(controls->removes, 1);
    QCOMPARE(controls->removed.value, 4812LL);
    QVERIFY(snapshot);
    QCOMPARE(snapshot->Invoke(nullptr, args.Get()), S_OK);
    QCoreApplication::processEvents();
    QCOMPARE(actions.count(), 0);
    controls->retained.Reset();
    snapshot.Reset();
    QVERIFY(weak.expired());
    QCOMPARE(controls->removes, 1);
  }
}

void TestWindowsSmtcLifetime::queuedAndReentrantClose()
{
  SystemComponent::Get().hello("smtc-test");
  QSignalSpy actions(&InputComponent::Get(), &InputComponent::hostInput);
  auto controls = Make<FakeControls>();
  auto args = Make<ModelButtonArgs>();
  HRESULT during = E_FAIL;
  {
    TaskbarComponentWin taskbar;
    taskbar.m_systemControls = controls;
    taskbar.initializeMediaTransportControls();
    QVERIFY(taskbar.m_initialized);
    QCOMPARE(controls->retained->Invoke(nullptr, args.Get()), S_OK);
    controls->duringRemove = [&] {
      QVERIFY(!taskbar.m_initialized);
      during = controls->retained->Invoke(nullptr, args.Get());
    };
  }
  QCOMPARE(during, S_OK);
  QCOMPARE(controls->removes, 1);
  QCOMPARE(controls->removed.value, 4812LL);
  QCoreApplication::processEvents();
  QCOMPARE(actions.count(), 0);
}

void TestWindowsSmtcLifetime::heldCallbackAndReplacement()
{
  SystemComponent::Get().hello("smtc-test");
  QSignalSpy actions(&InputComponent::Get(), &InputComponent::hostInput);
  auto controls = Make<FakeControls>();
  auto old = std::make_unique<TaskbarComponentWin>();
  old->m_systemControls = controls;
  old->initializeMediaTransportControls();
  QVERIFY(old->m_initialized);
  std::weak_ptr<SmtcAdmission> weak = old->m_admission;
  ComPtr<ButtonHandler> oldHandler = controls->retained;
  auto args = Make<BlockingButtonArgs>();
  args->gate = std::make_shared<HeldGetter>();
  JoinHeldWorker held{args->gate, {}};
  HRESULT result = E_FAIL;
  held.worker = std::thread([oldHandler, args, &result] { result = oldHandler->Invoke(nullptr, args.Get()); });
  bool entered = false;
  { std::unique_lock<std::mutex> lock(held.gate->mutex);
    entered = held.gate->changed.wait_for(lock, std::chrono::seconds(2), [&] { return held.gate->entered; }); }
  QVERIFY(entered);
  QElapsedTimer elapsed;
  elapsed.start();
  old.reset();
  const qint64 closeMs = elapsed.elapsed();
  bool timedOut;
  { std::lock_guard<std::mutex> lock(held.gate->mutex);
    timedOut = held.gate->timedOut; held.gate->released = true; }
  held.gate->changed.notify_all();
  held.worker.join();
  QVERIFY2(!timedOut && closeMs < 4000, "Destruction waited for the blocked COM getter");
  QCOMPARE(result, S_OK);
  QCOMPARE(controls->removes, 1);
  QCOMPARE(controls->removed.value, 4812LL);
  auto next = std::make_unique<TaskbarComponentWin>();
  next->m_systemControls = controls;
  next->initializeMediaTransportControls();
  QVERIFY(next->m_initialized);
  auto normal = Make<ModelButtonArgs>();
  QCOMPARE(oldHandler->Invoke(nullptr, normal.Get()), S_OK);
  QCOMPARE(controls->retained->Invoke(nullptr, normal.Get()), S_OK);
  QTRY_COMPARE_WITH_TIMEOUT(actions.count(), 1, 5000);
  QCOMPARE(actions.at(0).at(0).toStringList(), QStringList{"play_pause"});
  next.reset();
  QCOMPARE(controls->removes, 2);
  oldHandler.Reset();
  QVERIFY(weak.expired());
}

void TestWindowsSmtcLifetime::healthyAndGetterFailure()
{
  SystemComponent::Get().hello("smtc-test");
  QSignalSpy actions(&InputComponent::Get(), &InputComponent::hostInput);
  auto controls = Make<FakeControls>();
  TaskbarComponentWin taskbar;
  taskbar.m_systemControls = controls;
  taskbar.initializeMediaTransportControls();
  QVERIFY(taskbar.m_initialized);
  auto args = Make<ModelButtonArgs>();
  args->result = E_ACCESSDENIED;
  QCOMPARE(controls->retained->Invoke(nullptr, args.Get()), E_ACCESSDENIED);
  QCoreApplication::processEvents();
  QCOMPARE(actions.count(), 0);
  args->result = S_OK;
  ComPtr<ButtonHandler> handler = controls->retained;
  const QList<QPair<SystemMediaTransportControlsButton, QString>> mappings{
    {SystemMediaTransportControlsButton_Play, "play_pause"},
    {SystemMediaTransportControlsButton_Pause, "play_pause"},
    {SystemMediaTransportControlsButton_Next, "next"},
    {SystemMediaTransportControlsButton_Previous, "previous"},
    {SystemMediaTransportControlsButton_Stop, "stop"},
    {SystemMediaTransportControlsButton_FastForward, "seek_forward"},
    {SystemMediaTransportControlsButton_Rewind, "seek_backward"},
    {SystemMediaTransportControlsButton_ChannelUp, "channelup"},
    {SystemMediaTransportControlsButton_ChannelDown, "channeldown"}};
  for (const auto& mapping : mappings) {
    args->button = mapping.first;
    HRESULT result = E_FAIL;
    std::thread worker([handler, args, &result] { result = handler->Invoke(nullptr, args.Get()); });
    worker.join();
    QCOMPARE(result, S_OK);
    QTRY_COMPARE_WITH_TIMEOUT(actions.count(), 1, 5000);
    QCOMPARE(actions.takeFirst().at(0).toStringList(), QStringList{mapping.second});
  }
}

QTEST_GUILESS_MAIN(TestWindowsSmtcLifetime)
#include "windows_smtc_lifetime.moc"
