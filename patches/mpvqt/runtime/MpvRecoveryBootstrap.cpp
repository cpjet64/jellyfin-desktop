#include "MpvRecoveryBootstrap.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace MpvRecoveryBootstrap {
namespace {
struct Task;
std::mutex &registryMutex() { static auto *value = new std::mutex; return *value; }
std::vector<std::shared_ptr<Task>> &registry()
{
    static auto *value = new std::vector<std::shared_ptr<Task>>;
    return *value;
}

void finalizeAsync(const std::shared_ptr<RendererRuntime::CoreOwner> &owner)
{
    QMetaObject::invokeMethod(owner->controller, [owner] { owner->requestFinalization(); }, Qt::QueuedConnection);
}

struct Task : std::enable_shared_from_this<Task> {
    quint64 reservation;
    quint64 revision;
    Completion completion;
    QCoreApplication *dispatcher;
    MpvController *controller;
    QThread *worker;
    RendererRuntime::WakeRelay *relay;
    std::mutex dispatcherMutex;
    QMetaObject::Connection aboutToQuitConnection;
    QMetaObject::Connection destroyedConnection;

    Task(quint64 token, quint64 rev, Completion done, QCoreApplication *app,
         MpvController *control, QThread *thread, RendererRuntime::WakeRelay *wake)
        : reservation(token), revision(rev), completion(std::move(done)), dispatcher(app),
          controller(control), worker(thread), relay(wake) {}

    void release()
    {
        QObject::disconnect(aboutToQuitConnection);
        QObject::disconnect(destroyedConnection);
        std::lock_guard<std::mutex> lock(registryMutex());
        auto &tasks = registry();
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [this](const auto &task) {
            return task.get() == this;
        }), tasks.end());
    }
    bool postGui(std::function<void()> operation)
    {
        std::lock_guard<std::mutex> lock(dispatcherMutex);
        return dispatcher && QMetaObject::invokeMethod(dispatcher, std::move(operation), Qt::QueuedConnection);
    }
    void closeDispatcher()
    {
        std::lock_guard<std::mutex> lock(dispatcherMutex);
        dispatcher = nullptr;
    }
    void initialized(bool ok);
    void bind(const std::shared_ptr<RendererRuntime::CoreOwner> &owner);
};

void Task::bind(const std::shared_ptr<RendererRuntime::CoreOwner> &owner)
{
    auto self = shared_from_this();
    if (!QMetaObject::invokeMethod(controller, [self, owner] {
            self->controller->bindRecoveryCore(owner);
            if (self->controller->eventMode() == MpvController::EventMode::External &&
                !owner->installExternalWakeup()) {
                owner->requestFinalization();
                self->postGui([self] { self->completion({}, self->revision); self->release(); });
                return;
            }
            self->postGui([self, owner] {
                if (!self->completion(owner, self->revision)) finalizeAsync(owner);
                self->release();
            });
        }, Qt::QueuedConnection)) {
        finalizeAsync(owner);
        release();
    }
}

void Task::initialized(bool ok)
{
    auto owner = ok
        ? RendererRuntime::CoreOwner::adoptReserved(reservation, controller->mpv(), controller, worker, relay, dispatcher)
        : RendererRuntime::CoreOwner::adoptFailedReservation(reservation, controller, worker, relay, dispatcher);
    if (!owner) return; // Keep the task and reservation retained if adoption cannot be established.
    if (!ok) {
        finalizeAsync(owner);
        completion({}, revision);
        release();
        return;
    }
    bind(owner);
}
} // namespace

void start(quint64 reservation, quint64 revision, MpvController::EventMode mode, Completion completion)
{
    auto *dispatcher = QCoreApplication::instance();
    if (!dispatcher) {
        RendererRuntime::cancelCoreReservation(reservation);
        completion({}, revision);
        return;
    }
    static bool lifecycleConnected = false;
    if (!lifecycleConnected) {
        QObject::connect(dispatcher, &QCoreApplication::aboutToQuit, dispatcher, [dispatcher] {
            RendererRuntime::closeAdmissionDispatcher(dispatcher);
        }, Qt::DirectConnection);
        lifecycleConnected = true;
    }
    auto *worker = new QThread;
    auto *controller = new MpvController(mode);
    auto *relay = new RendererRuntime::WakeRelay;
    auto task = std::make_shared<Task>(reservation, revision, std::move(completion), dispatcher,
                                       controller, worker, relay);
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().push_back(task);
    }
    std::weak_ptr<Task> weak = task;
    task->aboutToQuitConnection = QObject::connect(dispatcher, &QCoreApplication::aboutToQuit, dispatcher, [weak] {
        if (auto retained = weak.lock()) retained->closeDispatcher();
    }, Qt::DirectConnection);
    task->destroyedConnection = QObject::connect(dispatcher, &QObject::destroyed, [weak] {
        if (auto retained = weak.lock()) retained->closeDispatcher();
    });
    controller->moveToThread(worker);
    relay->moveToThread(worker);
    worker->start();
    if (!QMetaObject::invokeMethod(controller, [task] {
            const bool ok = task->controller->initializeRecovery();
            task->postGui([task, ok] { task->initialized(ok); });
        }, Qt::QueuedConnection)) task->initialized(false);
}
} // namespace MpvRecoveryBootstrap
