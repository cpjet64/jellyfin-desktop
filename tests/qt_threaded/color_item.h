#pragma once

#include <QQuickFramebufferObject>
#include <atomic>
#include <memory>

// Acceptance stimulus only: alternates two bright colors on the render thread.
struct ColorObservation
{
  std::atomic<unsigned> calls{0};
  std::atomic<quintptr> renderThread{0};
  std::atomic<quintptr> context{0};
  std::atomic<quintptr> destructorThread{0};
  std::atomic<bool> wrongContext{false};
  std::atomic<bool> wrongThread{false};
};

class ColorItem : public QQuickFramebufferObject
{
public:
  explicit ColorItem(QQuickItem* parent = nullptr);
  Renderer* createRenderer() const override;
  std::shared_ptr<ColorObservation> observation() const { return m_observation; }
private:
  std::shared_ptr<ColorObservation> m_observation;
};
