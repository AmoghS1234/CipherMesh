#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include "../core/vault.hpp"
#include "vaultqmlwrapper.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Set application metadata
    app.setOrganizationName("CipherMesh");
    app.setApplicationName("CipherMesh Mobile");

    // 1. Initialize C++ Core
    CipherMesh::Core::Vault vault;
    
    // Android-specific data path
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QString dbFilePath = dataPath + "/vault.db";
    std::string dbPath = dbFilePath.toStdString();
    
    // Check if vault database exists
    bool vaultExists = QFile::exists(dbFilePath);
    
    // The vault will be unlocked/created via the UI
    // We just keep a reference to it here
    // If the vault doesn't exist, user will create it on first unlock

    // 2. Initialize Wrapper
    VaultQmlWrapper vaultWrapper(&vault);
    
    // Pass the database path to the wrapper so it can create/load the vault
    vaultWrapper.setProperty("dbPath", QString::fromStdString(dbPath));
    vaultWrapper.setProperty("vaultExists", vaultExists);

    // 3. QML Engine
    QQmlApplicationEngine engine;
    
    // 4. Inject Wrapper into QML context
    engine.rootContext()->setContextProperty("vaultBackend", &vaultWrapper);
    
    const QUrl url(QStringLiteral("qrc:/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}