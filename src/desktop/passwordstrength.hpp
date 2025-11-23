#pragma once

#include <QString>
#include <QColor>
#include "../utils/passwordstrength.hpp"

namespace CipherMesh {
namespace GUI {

// Qt wrapper for platform-independent password strength calculator
class PasswordStrengthCalculatorQt {
public:
    struct Info {
        QString text;
        QColor color;
        int score;
    };
    
    static Info calculate(const QString& password) {
        CipherMesh::Utils::PasswordStrengthInfo coreInfo = 
            CipherMesh::Utils::PasswordStrengthCalculator::calculate(
                password.toStdString()
            );
        
        Info qtInfo;
        qtInfo.text = QString::fromStdString(coreInfo.text);
        qtInfo.color = QColor(coreInfo.color.r, coreInfo.color.g, coreInfo.color.b);
        qtInfo.score = coreInfo.score;
        
        return qtInfo;
    }
};

} // namespace GUI
} // namespace CipherMesh
