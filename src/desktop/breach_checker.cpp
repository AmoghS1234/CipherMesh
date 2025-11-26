#include "breach_checker.hpp"
#include "../core/crypto.hpp"
#include <QNetworkRequest>
#include <QString>
#include <QDebug>

BreachChecker::BreachChecker(QObject* parent) : QObject(parent) {
    m_netManager = new QNetworkAccessManager(this);
}

void BreachChecker::checkPassword(const std::string& password, std::function<void(bool, int)> callback) {
    std::string hash = CipherMesh::Core::Crypto::sha1(password); // Returns uppercase hex
    std::string prefix = hash.substr(0, 5);
    std::string suffix = hash.substr(5);

    QString url = QString("https://api.pwnedpasswords.com/range/%1").arg(QString::fromStdString(prefix));
    QNetworkRequest request(url);
    
    QNetworkReply* reply = m_netManager->get(request);
    
    connect(reply, &QNetworkReply::finished, [reply, suffix, callback]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Breach check API failed:" << reply->errorString();
            callback(false, -1); // Return -1 to indicate error state (not truly safe)
            reply->deleteLater();
            return;
        }

        QString data = QString::fromUtf8(reply->readAll());
        QStringList lines = data.split('\n'); // Format: SUFFIX:COUNT
        
        bool found = false;
        int count = 0;

        for (const QString& line : lines) {
            QStringList parts = line.split(':');
            if (parts.size() >= 2) {
                if (parts[0].trimmed() == QString::fromStdString(suffix)) {
                    found = true;
                    count = parts[1].toInt();
                    break;
                }
            }
        }

        callback(found, count);
        reply->deleteLater();
    });
}
