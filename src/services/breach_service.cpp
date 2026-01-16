#include "breach_service.hpp"
#include "../core/crypto.hpp"

#if defined(__ANDROID__) || defined(ANDROID)
// Android implementation would use Java/Kotlin HTTP client
// This is just a stub for compilation
#include <android/log.h>
#define LOG_TAG "BreachService"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace CipherMesh {
namespace Services {

void BreachService::checkPassword(const std::string& password, 
                                  std::function<void(bool isCompromised, int count)> callback) {
    // Android implementation would call Java/Kotlin code via JNI
    // For now, just return safe (not implemented)
    LOGE("BreachService::checkPassword not fully implemented on Android - use Kotlin implementation");
    callback(false, -1);
}

std::string BreachService::getSha1Prefix(const std::string& password) {
    std::string hash = CipherMesh::Core::Crypto::sha1(password);
    return hash.substr(0, 5);
}

std::string BreachService::getSha1Suffix(const std::string& password) {
    std::string hash = CipherMesh::Core::Crypto::sha1(password);
    return hash.substr(5);
}

} // namespace Services
} // namespace CipherMesh

#else
// Desktop Qt implementation
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QString>
#include <QEventLoop>
#include <QTimer>

namespace CipherMesh {
namespace Services {

void BreachService::checkPassword(const std::string& password, 
                                  std::function<void(bool isCompromised, int count)> callback) {
    std::string hash = CipherMesh::Core::Crypto::sha1(password); // Returns uppercase hex
    std::string prefix = hash.substr(0, 5);
    std::string suffix = hash.substr(5);

    QString url = QString("https://api.pwnedpasswords.com/range/%1").arg(QString::fromStdString(prefix));
    QNetworkRequest request(url);
    
    // Create network manager on heap (will be deleted via parent chain)
    QNetworkAccessManager* netManager = new QNetworkAccessManager();
    QNetworkReply* reply = netManager->get(request);
    
    QObject::connect(reply, &QNetworkReply::finished, [reply, suffix, callback, netManager]() {
        if (reply->error() != QNetworkReply::NoError) {
            callback(false, -1); // Return -1 to indicate error state
            reply->deleteLater();
            netManager->deleteLater();
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
        netManager->deleteLater();
    });
}

std::string BreachService::getSha1Prefix(const std::string& password) {
    std::string hash = CipherMesh::Core::Crypto::sha1(password);
    return hash.substr(0, 5);
}

std::string BreachService::getSha1Suffix(const std::string& password) {
    std::string hash = CipherMesh::Core::Crypto::sha1(password);
    return hash.substr(5);
}

} // namespace Services
} // namespace CipherMesh

#endif
