#pragma once

#include <QDialog>
#include <string>

class QLineEdit;
class QSlider;
class QCheckBox;
class QLabel;
class QProgressBar;

class PasswordGeneratorDialog : public QDialog {
    Q_OBJECT

public:
    explicit PasswordGeneratorDialog(QWidget *parent = nullptr);
    std::string getPassword() const;

private slots:
    void generatePassword();
    void onSliderChanged(int value);
    void onSymbolCheckChanged(int state); // <-- NEW SLOT
    void updateStrengthMeter();

private:
    void setupUi();
    
    std::string m_password;

    QLineEdit* m_passwordEdit;
    QLabel* m_lengthLabel;
    QSlider* m_lengthSlider;
    QCheckBox* m_upperCheck;
    QCheckBox* m_lowerCheck;
    QCheckBox* m_numberCheck;
    QCheckBox* m_symbolCheck;
    QLineEdit* m_symbolEdit; // <-- NEW
    QProgressBar* m_strengthBar;
    QLabel* m_strengthLabel;
};