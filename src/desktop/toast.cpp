#include "toast.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QApplication>
#include <QScreen>

namespace CipherMesh {
namespace GUI {

Toast::Toast(const QString& message, ToastType type, QWidget *parent)
    : QWidget(parent),
      m_message(message),
      m_type(type),
      m_opacity(1.0)
{
    setupUi();
    
    // Auto-hide after 3 seconds
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Toast::fadeOut);
}

void Toast::setupUi()
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    
    m_label = new QLabel(getIcon() + " " + m_message);
    m_label->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: 500;")
                          .arg(getTextColor().name()));
    m_label->setWordWrap(true);
    
    layout->addWidget(m_label);
    
    setMinimumWidth(300);
    setMaximumWidth(500);
    adjustSize();
}

void Toast::show()
{
    QWidget::show();
    
    // Position at bottom-right of parent or screen
    QWidget* p = parentWidget();
    if (p) {
        QPoint localBottomRight(p->width() - width() - 20, p->height() - height() - 20);
        QPoint globalPos = p->mapToGlobal(localBottomRight);
        move(globalPos);
    } else {
        QScreen* screen = QApplication::primaryScreen();
        QRect screenGeometry = screen->geometry();
        move(screenGeometry.width() - width() - 20, screenGeometry.height() - height() - 80);
    }
    
    m_timer->start(3000);
}

void Toast::fadeOut()
{
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);
    
    QPropertyAnimation* animation = new QPropertyAnimation(effect, "opacity");
    animation->setDuration(500);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::OutQuad);
    
    connect(animation, &QPropertyAnimation::finished, this, [this]() {
        emit closed();
        deleteLater();
    });
    
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void Toast::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw rounded rectangle background
    painter.setBrush(getBackgroundColor());
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), 8, 8);
}

QColor Toast::getBackgroundColor() const
{
    switch (m_type) {
        case ToastType::Success:
            return QColor(76, 175, 80, 230); // Green with transparency
        case ToastType::Error:
            return QColor(211, 47, 47, 230); // Red with transparency
        case ToastType::Warning:
            return QColor(255, 152, 0, 230); // Orange with transparency
        case ToastType::Info:
        default:
            return QColor(33, 150, 243, 230); // Blue with transparency
    }
}

QColor Toast::getTextColor() const
{
    return QColor(255, 255, 255); // White text for all types
}

QString Toast::getIcon() const
{
    switch (m_type) {
        case ToastType::Success:
            return "✓";
        case ToastType::Error:
            return "✗";
        case ToastType::Warning:
            return "⚠";
        case ToastType::Info:
        default:
            return "ℹ";
    }
}

} // namespace GUI
} // namespace CipherMesh
