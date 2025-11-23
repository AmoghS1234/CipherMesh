#include "locationeditdialog.hpp"
#include "vault_entry.hpp" // <-- FIXED PATH
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>

LocationEditDialog::LocationEditDialog(QWidget *parent)
    : QDialog(parent), m_editingId(-1) {
    setupUi();
    setWindowTitle("Add Location");
    onTypeChanged(0); // Set initial hint text
}

LocationEditDialog::LocationEditDialog(const CipherMesh::Core::Location& location, QWidget *parent)
    : QDialog(parent), m_editingId(location.id) {
    setupUi();
    setWindowTitle("Edit Location");
    
    // Pre-fill data
    m_valueEdit->setText(QString::fromStdString(location.value));
    
    // Find the type in the combo box, or select "Other"
    int index = m_typeCombo->findText(QString::fromStdString(location.type));
    if (index != -1) {
        m_typeCombo->setCurrentIndex(index);
    } else {
        m_typeCombo->setCurrentIndex(m_typeCombo->count() - 1); // "Other"
        m_customTypeEdit->setText(QString::fromStdString(location.type));
    }
    onTypeChanged(m_typeCombo->currentIndex());
}

void LocationEditDialog::setupUi() {
    setModal(true);
    setMinimumWidth(500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    QLabel* titleLabel = new QLabel("Location Details", this);
    titleLabel->setObjectName("DialogTitle");
    mainLayout->addWidget(titleLabel);
    
    m_form = new QFormLayout();
    m_form->setSpacing(12);
    m_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem("URL");
    m_typeCombo->addItem("Android App");
    m_typeCombo->addItem("iOS App");
    m_typeCombo->addItem("Wi-Fi SSID");
    m_typeCombo->addItem("Other...");
    
    m_customTypeEdit = new QLineEdit(this);
    m_customTypeEdit->setPlaceholderText("Enter custom type (e.g., Steam)");
    
    m_valueEdit = new QLineEdit(this);
    
    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setObjectName("PlaceholderLabel");

    m_form->addRow("Type:", m_typeCombo);
    m_form->addRow("Custom Type:", m_customTypeEdit);
    m_form->addRow("Value:", m_valueEdit);
    m_form->addRow("", m_hintLabel);

    mainLayout->addLayout(m_form);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setMinimumWidth(100);
    buttonBox->button(QDialogButtonBox::Cancel)->setMinimumWidth(100);
    mainLayout->addWidget(buttonBox);
    
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &LocationEditDialog::onTypeChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &LocationEditDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void LocationEditDialog::onTypeChanged(int index) {
    QString selectedType = m_typeCombo->currentText();
    
    if (selectedType == "URL") {
        m_hintLabel->setText("e.g., https://google.com");
        m_customTypeEdit->setVisible(false);
    } else if (selectedType == "Android App") {
        m_hintLabel->setText("e.g., com.google.android.gm");
        m_customTypeEdit->setVisible(false);
    } else if (selectedType == "iOS App") {
        m_hintLabel->setText("e.g., com.google.Gmail");
        m_customTypeEdit->setVisible(false);
    } else if (selectedType == "Wi-Fi SSID") {
        m_hintLabel->setText("e.g., MyHomeNetwork");
        m_customTypeEdit->setVisible(false);
    } else if (selectedType == "Other...") {
        m_hintLabel->setText("Enter a custom value. This may not work with autofill.");
        m_customTypeEdit->setVisible(true);
    }
    
    // Find the label for "Custom Type:" and hide it too
    QWidget* customLabel = m_form->labelForField(m_customTypeEdit); // <-- FIXED
    if(customLabel) customLabel->setVisible(m_customTypeEdit->isVisible());
}

void LocationEditDialog::onAccept() {
    // Basic validation
    if (m_valueEdit->text().isEmpty()) {
        m_hintLabel->setText("Value cannot be empty.");
        m_hintLabel->setStyleSheet("color: #ff5555;");
        return;
    }
    if (m_typeCombo->currentText() == "Other..." && m_customTypeEdit->text().isEmpty()) {
        m_hintLabel->setText("Custom type cannot be empty.");
        m_hintLabel->setStyleSheet("color: #ff5555;");
        return;
    }
    accept();
}

CipherMesh::Core::Location LocationEditDialog::getLocationData() const {
    CipherMesh::Core::Location loc;
    loc.id = m_editingId;
    loc.value = m_valueEdit->text().toStdString();
    
    if (m_typeCombo->currentText() == "Other...") {
        loc.type = m_customTypeEdit->text().toStdString();
    } else {
        loc.type = m_typeCombo->currentText().toStdString();
    }
    return loc;
}