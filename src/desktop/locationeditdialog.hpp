#pragma once

#include <QDialog>
#include <QString>

namespace CipherMesh { namespace Core { struct Location; } }

class QComboBox;
class QLineEdit;
class QLabel;
class QFormLayout; // <-- NEW FORWARD DECLARATION

class LocationEditDialog : public QDialog {
    Q_OBJECT

public:
    // Constructor for creating a new location
    explicit LocationEditDialog(QWidget *parent = nullptr);
    // Constructor for editing an existing location
    explicit LocationEditDialog(const CipherMesh::Core::Location& location, QWidget *parent = nullptr);

    CipherMesh::Core::Location getLocationData() const;

private slots:
    void onTypeChanged(int index);
    void onAccept();

private:
    void setupUi();

    int m_editingId; // -1 for new, >0 for existing

    QFormLayout* m_form; // <-- NEW MEMBER VARIABLE
    QComboBox* m_typeCombo;
    QLineEdit* m_valueEdit;
    QLabel* m_hintLabel;
    QLineEdit* m_customTypeEdit;
};