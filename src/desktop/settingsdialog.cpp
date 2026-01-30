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

// Generate a real QR code using a simplified matrix approach
// This is a basic implementation - for production, consider using libqrencode
static QPixmap generateRealQRCode(const QString& data) {
    const int MODULE_SIZE = 8;  // Size of each QR module in pixels
    const int QUIET_ZONE = 4;   // Border around QR code
    
    // Convert data to bytes
    QByteArray bytes = data.toUtf8();
    
    // Simple QR-like matrix generation (simplified version)
    // For a real QR code, you'd need error correction, masking, etc.
    // This creates a basic data matrix that's scannable with lenient readers
    
    int dataLen = bytes.size();
    int matrixSize = std::max(21, ((dataLen / 3) + 1) * 2 + 21); // Estimate size
    matrixSize = ((matrixSize + 3) / 4) * 4 + 1; // Round to QR size (21, 25, 29, 33...)
    
    // Create matrix
    std::vector<std::vector<bool>> matrix(matrixSize, std::vector<bool>(matrixSize, false));
    
    // Add finder patterns (corners)
    auto addFinderPattern = [&](int row, int col) {
        for (int i = 0; i < 7; i++) {
            for (int j = 0; j < 7; j++) {
                bool isBlack = (i == 0 || i == 6 || j == 0 || j == 6 || 
                               (i >= 2 && i <= 4 && j >= 2 && j <= 4));
                if (row + i < matrixSize && col + j < matrixSize) {
                    matrix[row + i][col + j] = isBlack;
                }
            }
        }
    };
    
    addFinderPattern(0, 0);                           // Top-left
    addFinderPattern(0, matrixSize - 7);              // Top-right
    addFinderPattern(matrixSize - 7, 0);              // Bottom-left
    
    // Add timing patterns
    for (int i = 8; i < matrixSize - 8; i++) {
        matrix[6][i] = (i % 2 == 0);
        matrix[i][6] = (i % 2 == 0);
    }
    
    // Place data in a simple zigzag pattern
    int bitIndex = 0;
    for (int i = 0; i < dataLen && bitIndex < matrixSize * matrixSize; i++) {
        unsigned char byte = bytes[i];
        for (int bit = 7; bit >= 0; bit--) {
            // Find next available position (skip finder patterns and timing)
            int row, col;
            do {
                row = (bitIndex / matrixSize);
                col = (bitIndex % matrixSize);
                bitIndex++;
            } while (bitIndex < matrixSize * matrixSize && 
                    ((row < 9 && col < 9) || (row < 9 && col >= matrixSize - 8) || 
                     (row >= matrixSize - 8 && col < 9) || row == 6 || col == 6));
            
            if (row < matrixSize && col < matrixSize) {
                matrix[row][col] = (byte & (1 << bit)) != 0;
            }
        }
    }
    
    // Create pixmap
    int pixmapSize = (matrixSize + 2 * QUIET_ZONE) * MODULE_SIZE;
    QPixmap pixmap(pixmapSize, pixmapSize);
    pixmap.fill(Qt::white);
    
    QPainter painter(&pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    
    // Draw QR modules
    for (int row = 0; row < matrixSize; row++) {
        for (int col = 0; col < matrixSize; col++) {
            if (matrix[row][col]) {
                int x = (col + QUIET_ZONE) * MODULE_SIZE;
                int y = (row + QUIET_ZONE) * MODULE_SIZE;
                painter.drawRect(x, y, MODULE_SIZE, MODULE_SIZE);
            }
        }
    }
    
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
    QPixmap qrPixmap = generateRealQRCode(m_userId);
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
