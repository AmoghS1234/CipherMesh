#include "newgroupdialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace CipherMesh::GUI {

NewGroupDialog::NewGroupDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUI();
    
    connect(m_createButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_groupNameEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
}

void NewGroupDialog::setupUI()
{
    setWindowTitle("Create New Group");
    setMinimumWidth(500);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    // Title with emoji
    auto* titleLabel = new QLabel("📁 Create New Group");
    titleLabel->setObjectName("DialogTitle");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);
    
    // Subtitle/description
    auto* subtitleLabel = new QLabel("Enter a name for your new password group.");
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet("color: #808080;");
    mainLayout->addWidget(subtitleLabel);
    
    mainLayout->addSpacing(8);
    
    // Group name input
    auto* nameLabel = new QLabel("Group Name:");
    mainLayout->addWidget(nameLabel);
    
    m_groupNameEdit = new QLineEdit();
    m_groupNameEdit->setPlaceholderText("e.g., Work, Personal, Banking...");
    m_groupNameEdit->setMinimumHeight(40);
    mainLayout->addWidget(m_groupNameEdit);
    
    mainLayout->addStretch();
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel");
    m_cancelButton->setObjectName("CancelButton");
    m_cancelButton->setMinimumWidth(100);
    m_cancelButton->setMinimumHeight(40);
    buttonLayout->addWidget(m_cancelButton);
    
    m_createButton = new QPushButton("Create Group");
    m_createButton->setObjectName("NewButton");
    m_createButton->setMinimumWidth(120);
    m_createButton->setMinimumHeight(40);
    m_createButton->setDefault(true);
    buttonLayout->addWidget(m_createButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Focus on input field
    m_groupNameEdit->setFocus();
}

QString NewGroupDialog::groupName() const
{
    return m_groupNameEdit->text().trimmed();
}

} // namespace CipherMesh::GUI
