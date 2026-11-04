#include "color_item.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>

class ColorRenderer final : public QQuickFramebufferObject::Renderer
{
public:
  explicit ColorRenderer(std::shared_ptr<ColorObservation> observation)
    : m_observation(std::move(observation)) {}
  ~ColorRenderer() override
  { m_observation->destructorThread = reinterpret_cast<quintptr>(QThread::currentThreadId()); }

  void render() override
  {
    auto* context = QOpenGLContext::currentContext();
    if (!context) { m_observation->wrongContext = true; return; }
    const auto pointer = reinterpret_cast<quintptr>(context);
    quintptr initial = 0;
    m_observation->context.compare_exchange_strong(initial, pointer);
    if (m_observation->context.load() != pointer) m_observation->wrongContext = true;
    const auto thread = reinterpret_cast<quintptr>(QThread::currentThreadId());
    quintptr firstThread = 0;
    m_observation->renderThread.compare_exchange_strong(firstThread, thread);
    if (m_observation->renderThread.load() != thread) m_observation->wrongThread = true;
    const auto count = ++m_observation->calls;
    auto* gl = context->functions();
    if (count % 2) gl->glClearColor(0.9f, 0.1f, 0.1f, 1.0f);
    else gl->glClearColor(0.1f, 0.8f, 0.1f, 1.0f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    update();
  }
private:
  std::shared_ptr<ColorObservation> m_observation;
};

ColorItem::ColorItem(QQuickItem* parent)
  : QQuickFramebufferObject(parent), m_observation(std::make_shared<ColorObservation>())
{
}

QQuickFramebufferObject::Renderer* ColorItem::createRenderer() const
{
  return new ColorRenderer(m_observation);
}
