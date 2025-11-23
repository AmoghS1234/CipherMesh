#ifndef NEWGROUPDIALOG_HPP
#define NEWGROUPDIALOG_HPP

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

namespace CipherMesh::GUI {

class NewGroupDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewGroupDialog(QWidget* parent = nullptr);
    ~NewGroupDialog() override = default;

    QString groupName() const;

private:
    QLineEdit* m_groupNameEdit;
    QPushButton* m_createButton;
    QPushButton* m_cancelButton;

    void setupUI();
};

} // namespace CipherMesh::GUI

#endif // NEWGROUPDIALOG_HPP
