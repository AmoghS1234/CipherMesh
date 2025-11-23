#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QTimer>

class QLabel;

namespace CipherMesh {
namespace GUI {

enum class ToastType {
    Success,
    Error,
    Warning,
    Info
};

class Toast : public QWidget {
    Q_OBJECT

public:
    explicit Toast(const QString& message, ToastType type = ToastType::Info, QWidget *parent = nullptr);
    ~Toast() = default;
    
    void show();
    
signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void fadeOut();

private:
    void setupUi();
    QColor getBackgroundColor() const;
    QColor getTextColor() const;
    QString getIcon() const;
    
    QString m_message;
    ToastType m_type;
    QLabel* m_label;
    QTimer* m_timer;
    double m_opacity;
};

} // namespace GUI
} // namespace CipherMesh
