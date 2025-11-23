#include "settingsdialog.hpp"
#include "vault.hpp"
#include "themes.hpp"
#include "toast.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QPixmap>
#include <QPainter>
#include <QTimer>

// Simple QR code generation using ASCII art for display
// For a real implementation, consider using a library like qrencode
static QPixmap generateSimpleQRPlaceholder(const QString& data) {
    const int PIXMAP_SIZE = 300;
    const int BORDER = 10;
    const int CORNER_SIZE = 80;
    const int CORNER_OFFSET = 20;
    const int PATTERN_SIZE = 12;
    const int PATTERN_BLOCK_SIZE = 10;
    const int TEXT_Y_OFFSET = 260;
    const int TEXT_HEIGHT = 40;
    
    QPixmap pixmap(PIXMAP_SIZE, PIXMAP_SIZE);
    pixmap.fill(Qt::white);
    
    QPainter painter(&pixmap);
    painter.setPen(Qt::black);
    
    // Draw outer border
    painter.drawRect(BORDER, BORDER, PIXMAP_SIZE - 2*BORDER, PIXMAP_SIZE - 2*BORDER);
    
    // Draw corner markers
    painter.drawRect(CORNER_OFFSET, CORNER_OFFSET, CORNER_SIZE, CORNER_SIZE);
    painter.drawRect(PIXMAP_SIZE - CORNER_OFFSET - CORNER_SIZE, CORNER_OFFSET, CORNER_SIZE, CORNER_SIZE);
    painter.drawRect(CORNER_OFFSET, PIXMAP_SIZE - CORNER_OFFSET - CORNER_SIZE, CORNER_SIZE, CORNER_SIZE);
    
    // Draw some pattern blocks to simulate QR
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            if ((i + j) % 2 == 0) {
                painter.fillRect(120 + i * PATTERN_SIZE, 120 + j * PATTERN_SIZE, 
                               PATTERN_BLOCK_SIZE, PATTERN_BLOCK_SIZE, Qt::black);
            }
        }
    }
    
    // Add text at bottom
    painter.setFont(QFont("Arial", 8));
    painter.drawText(QRect(0, TEXT_Y_OFFSET, PIXMAP_SIZE, TEXT_HEIGHT), 
                     Qt::AlignCenter | Qt::TextWordWrap, "User ID: " + data);
    
    return pixmap;
}

SettingsDialog::SettingsDialog(const QString& userId, CipherMesh::Core::Vault* vault, QWidget *parent)
    : QDialog(parent),
      m_userId(userId),
      m_vault(vault)
{
    setWindowTitle("Settings");
    setMinimumWidth(500);
    setupUi();
}

void SettingsDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(20);
    
    // User Identity Section
    QLabel* identityHeader = new QLabel("User Identity");
    identityHeader->setObjectName("DialogTitle");
    mainLayout->addWidget(identityHeader);
    
    QFormLayout* identityLayout = new QFormLayout();
    identityLayout->setSpacing(12);
    identityLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    m_userIdEdit = new QLineEdit(m_userId);
    m_userIdEdit->setReadOnly(true);
    
    m_copyUserIdButton = new QPushButton("Copy");
    m_copyUserIdButton->setFixedWidth(80);
    connect(m_copyUserIdButton, &QPushButton::clicked, this, &SettingsDialog::onCopyUserId);
    
    m_showQRButton = new QPushButton("Show QR Code");
    m_showQRButton->setMinimumWidth(140);
    connect(m_showQRButton, &QPushButton::clicked, this, &SettingsDialog::onShowQRCode);
    
    QHBoxLayout* userIdLayout = new QHBoxLayout();
    userIdLayout->setSpacing(8);
    userIdLayout->addWidget(m_userIdEdit);
    userIdLayout->addWidget(m_copyUserIdButton);
    
    identityLayout->addRow("User ID:", userIdLayout);
    identityLayout->addRow("", m_showQRButton);
    
    mainLayout->addLayout(identityLayout);
    
    // Theme Section
    QLabel* themeHeader = new QLabel("Appearance");
    themeHeader->setObjectName("DialogTitle");
    mainLayout->addWidget(themeHeader);
    
    QFormLayout* themeLayout = new QFormLayout();
    themeLayout->setSpacing(12);
    themeLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    m_themeComboBox = new QComboBox();
    for(const auto& theme : CipherMesh::Themes::Available) {
        m_themeComboBox->addItem(theme.name, theme.id);
    }
    
    // Set current theme if vault is available
    if (m_vault) {
        QString currentTheme = QString::fromStdString(m_vault->getThemeId());
        for (int i = 0; i < m_themeComboBox->count(); ++i) {
            if (m_themeComboBox->itemData(i).toString() == currentTheme) {
                m_themeComboBox->setCurrentIndex(i);
                break;
            }
        }
    }
    
    connect(m_themeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onThemeChanged);
    
    themeLayout->addRow("Theme:", m_themeComboBox);
    
    mainLayout->addLayout(themeLayout);
    
    // Auto-lock Section
    QLabel* securityHeader = new QLabel("Security");
    securityHeader->setObjectName("DialogTitle");
    mainLayout->addWidget(securityHeader);
    
    QFormLayout* securityLayout = new QFormLayout();
    securityLayout->setSpacing(12);
    securityLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    m_autoLockComboBox = new QComboBox();
    m_autoLockComboBox->addItem("Never", 0);
    m_autoLockComboBox->addItem("5 minutes", 5);
    m_autoLockComboBox->addItem("10 minutes", 10);
    m_autoLockComboBox->addItem("15 minutes", 15);
    m_autoLockComboBox->addItem("30 minutes", 30);
    m_autoLockComboBox->addItem("1 hour", 60);
    m_autoLockComboBox->addItem("2 hours", 120);
    
    // Set current auto-lock timeout if vault is available
    if (m_vault) {
        int currentTimeout = m_vault->getAutoLockTimeout();
        for (int i = 0; i < m_autoLockComboBox->count(); ++i) {
            if (m_autoLockComboBox->itemData(i).toInt() == currentTimeout) {
                m_autoLockComboBox->setCurrentIndex(i);
                break;
            }
        }
    }
    
    connect(m_autoLockComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onAutoLockChanged);
    
    securityLayout->addRow("Auto-lock after:", m_autoLockComboBox);
    
    mainLayout->addLayout(securityLayout);
    mainLayout->addStretch();
    
    // Close button
    QPushButton* closeButton = new QPushButton("Close");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    
    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::onCopyUserId()
{
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(m_userId);
    
    // Show toast notification
    using namespace CipherMesh::GUI;
    Toast* toast = new Toast("User ID copied to clipboard", ToastType::Success, this);
    toast->show();
    
    m_copyUserIdButton->setText("Copied!");
    QTimer::singleShot(2000, this, [this]() {
        m_copyUserIdButton->setText("Copy");
    });
}

void SettingsDialog::onShowQRCode()
{
    QDialog qrDialog(this);
    qrDialog.setWindowTitle("User ID QR Code");
    qrDialog.setMinimumSize(400, 500);
    
    QVBoxLayout* layout = new QVBoxLayout(&qrDialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    
    QLabel* titleLabel = new QLabel("Scan this QR code to share your User ID");
    titleLabel->setObjectName("DialogTitle");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    
    QLabel* qrLabel = new QLabel();
    QPixmap qrPixmap = generateSimpleQRPlaceholder(m_userId);
    qrLabel->setPixmap(qrPixmap);
    qrLabel->setAlignment(Qt::AlignCenter);
    qrLabel->setScaledContents(false);
    layout->addWidget(qrLabel);
    
    QLabel* textLabel = new QLabel(m_userId);
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setStyleSheet("font-family: monospace; font-size: 11px; padding: 10px; background-color: rgba(255,255,255,0.1); border-radius: 4px;");
    textLabel->setWordWrap(true);
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(textLabel);
    
    layout->addStretch();
    
    QPushButton* closeBtn = new QPushButton("Close");
    closeBtn->setMinimumWidth(100);
    closeBtn->setDefault(true);
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeBtn);
    layout->addLayout(buttonLayout);
    
    connect(closeBtn, &QPushButton::clicked, &qrDialog, &QDialog::accept);
    
    qrDialog.exec();
}

void SettingsDialog::onThemeChanged(int index)
{
    QString themeId = m_themeComboBox->itemData(index).toString();
    emit themeChanged(themeId);
}

void SettingsDialog::onAutoLockChanged(int index)
{
    int timeout = m_autoLockComboBox->itemData(index).toInt();
    if (m_vault) {
        m_vault->setAutoLockTimeout(timeout);
    }
    emit autoLockTimeoutChanged(timeout);
}
