#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>
#include <string>

class BreachChecker : public QObject {
    Q_OBJECT
public:
    explicit BreachChecker(QObject* parent = nullptr);
    
    // Returns true via callback if compromised, false if safe
    void checkPassword(const std::string& password, std::function<void(bool isCompromised, int count)> callback);

private:
    QNetworkAccessManager* m_netManager;
};
