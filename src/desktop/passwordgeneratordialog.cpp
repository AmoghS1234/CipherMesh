#include "passwordgeneratordialog.hpp"
#include "crypto.hpp" 
#include "passwordstrength.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QProgressBar>

PasswordGeneratorDialog::PasswordGeneratorDialog(QWidget *parent)
    : QDialog(parent) {
    setupUi();
    setWindowTitle("Password Generator");
    generatePassword(); // Generate one immediately
}

std::string PasswordGeneratorDialog::getPassword() const {
    return m_password;
}

void PasswordGeneratorDialog::setupUi() {
    setModal(true);
    setMinimumWidth(500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    // --- Preview ---
    QLabel* titleLabel = new QLabel("Password Generator", this);
    titleLabel->setObjectName("DialogTitle");
    mainLayout->addWidget(titleLabel);
    
    QHBoxLayout* previewLayout = new QHBoxLayout();
    previewLayout->setSpacing(8);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setReadOnly(true);
    m_passwordEdit->setObjectName("PasswordPreview");
    QPushButton* regenButton = new QPushButton("Regenerate");
    regenButton->setFixedWidth(120);
    previewLayout->addWidget(m_passwordEdit, 1);
    previewLayout->addWidget(regenButton);
    mainLayout->addLayout(previewLayout);
    
    // --- Strength Meter ---
    QHBoxLayout* strengthLayout = new QHBoxLayout();
    strengthLayout->setSpacing(8);
    m_strengthBar = new QProgressBar(this);
    m_strengthBar->setMaximum(100);
    m_strengthBar->setTextVisible(false);
    m_strengthBar->setMaximumHeight(12);
    m_strengthLabel = new QLabel("", this);
    m_strengthLabel->setMinimumWidth(120);
    strengthLayout->addWidget(m_strengthBar, 1);
    strengthLayout->addWidget(m_strengthLabel);
    mainLayout->addLayout(strengthLayout);

    // --- Options ---
    QFormLayout* optionsLayout = new QFormLayout();
    optionsLayout->setSpacing(12);
    optionsLayout->setContentsMargins(0, 12, 0, 12);
    optionsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    // Length
    m_lengthLabel = new QLabel("16", this);
    m_lengthLabel->setMinimumWidth(40);
    m_lengthSlider = new QSlider(Qt::Horizontal, this);
    m_lengthSlider->setMinimum(8);
    m_lengthSlider->setMaximum(64);
    m_lengthSlider->setValue(16);
    QHBoxLayout* lengthLayout = new QHBoxLayout();
    lengthLayout->setSpacing(12);
    lengthLayout->addWidget(m_lengthSlider, 1);
    lengthLayout->addWidget(m_lengthLabel);
    optionsLayout->addRow("Length:", lengthLayout);

    // Checkboxes
    m_upperCheck = new QCheckBox("Uppercase (A-Z)", this);
    m_upperCheck->setChecked(true);
    optionsLayout->addRow(m_upperCheck);
    
    m_lowerCheck = new QCheckBox("Lowercase (a-z)", this);
    m_lowerCheck->setChecked(true);
    optionsLayout->addRow(m_lowerCheck);

    m_numberCheck = new QCheckBox("Numbers (0-9)", this);
    m_numberCheck->setChecked(true);
    optionsLayout->addRow(m_numberCheck);

    // --- Symbol Layout ---
    m_symbolCheck = new QCheckBox("Symbols", this);
    m_symbolCheck->setChecked(true);
    m_symbolEdit = new QLineEdit(this);
    m_symbolEdit->setText(QString::fromStdString(CipherMesh::Core::Crypto::PasswordOptions().customSymbols));
    optionsLayout->addRow(m_symbolCheck, m_symbolEdit);
    
    mainLayout->addLayout(optionsLayout);

    // --- Buttons ---
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setMinimumWidth(100);
    buttonBox->button(QDialogButtonBox::Cancel)->setMinimumWidth(100);
    mainLayout->addWidget(buttonBox);

    // --- Connections ---
    connect(regenButton, &QPushButton::clicked, this, &PasswordGeneratorDialog::generatePassword);
    connect(m_lengthSlider, &QSlider::valueChanged, this, &PasswordGeneratorDialog::onSliderChanged);
    
    // Use checkStateChanged for Qt 6.7+ (where stateChanged is deprecated), otherwise use stateChanged
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(m_upperCheck, &QCheckBox::checkStateChanged, this, &PasswordGeneratorDialog::generatePassword);
    connect(m_lowerCheck, &QCheckBox::checkStateChanged, this, &PasswordGeneratorDialog::generatePassword);
    connect(m_numberCheck, &QCheckBox::checkStateChanged, this, &PasswordGeneratorDialog::generatePassword);
    
    // --- UPDATED SYMBOL CONNECTIONS ---
    connect(m_symbolCheck, &QCheckBox::checkStateChanged, this, &PasswordGeneratorDialog::onSymbolCheckChanged);
#else
    connect(m_upperCheck, &QCheckBox::stateChanged, this, &PasswordGeneratorDialog::generatePassword);
    connect(m_lowerCheck, &QCheckBox::stateChanged, this, &PasswordGeneratorDialog::generatePassword);
    connect(m_numberCheck, &QCheckBox::stateChanged, this, &PasswordGeneratorDialog::generatePassword);
    
    // --- UPDATED SYMBOL CONNECTIONS ---
    connect(m_symbolCheck, &QCheckBox::stateChanged, this, &PasswordGeneratorDialog::onSymbolCheckChanged);
#endif
    connect(m_symbolEdit, &QLineEdit::textChanged, this, &PasswordGeneratorDialog::generatePassword);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Call this once to set initial state
    onSymbolCheckChanged(m_symbolCheck->checkState());
}

void PasswordGeneratorDialog::onSliderChanged(int value) {
    m_lengthLabel->setText(QString::number(value));
    generatePassword();
}

void PasswordGeneratorDialog::onSymbolCheckChanged(int state) {
    // Enable/disable the text box based on the checkbox
    m_symbolEdit->setEnabled(state == Qt::Checked);
    generatePassword();
}

void PasswordGeneratorDialog::generatePassword() {
    CipherMesh::Core::Crypto::PasswordOptions options;
    options.length = m_lengthSlider->value();
    options.useUppercase = m_upperCheck->isChecked();
    options.useLowercase = m_lowerCheck->isChecked();
    options.useNumbers = m_numberCheck->isChecked();

    // --- UPDATED SYMBOL LOGIC ---
    if (m_symbolCheck->isChecked()) {
        options.customSymbols = m_symbolEdit->text().toStdString();
    } else {
        options.customSymbols = ""; // Send an empty string
    }

    try {
        m_password = CipherMesh::Core::Crypto::generatePassword(options);
        m_passwordEdit->setText(QString::fromStdString(m_password));
        updateStrengthMeter();
    } catch (const std::exception& e) {
        m_passwordEdit->setText(e.what());
    }
}

void PasswordGeneratorDialog::updateStrengthMeter() {
    using namespace CipherMesh::GUI;
    
    auto info = PasswordStrengthCalculatorQt::calculate(
        QString::fromStdString(m_password)
    );
    
    m_strengthBar->setValue(info.score);
    m_strengthLabel->setText(info.text);
    m_strengthLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(info.color.name()));
    
    // Dynamically color the progress bar chunk based on strength
    QString chunkColor = info.color.name();
    m_strengthBar->setStyleSheet(QString(
        "QProgressBar::chunk { background-color: %1; }"
    ).arg(chunkColor));
}