#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QDir>
#include "../core/vault.hpp"
#include "vaultqmlwrapper.hpp" // Include the wrapper

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 1. Initialize C++ Core
    CipherMesh::Core::Vault vault;
    
    // Android-specific data path
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataPath);
    if (!dir.exists()) dir.mkpath(".");
    
    std::string dbPath = dataPath.toStdString() + "/vault.db";
    
    // Attempt to load existing or create new (simplified logic for V1)
    if (!vault.loadVault(dbPath, "test1234")) { 
        // If load fails (doesn't exist), create it
        vault.createNewVault(dbPath, "test1234");
    }

    // 2. Initialize Wrapper
    VaultQmlWrapper vaultWrapper(&vault);

    // 3. QML Engine
    QQmlApplicationEngine engine;
    
    // 4. Inject Wrapper into QML context
    // Now QML can say: vaultBackend.unlockVault("...")
    engine.rootContext()->setContextProperty("vaultBackend", &vaultWrapper);
    
    const QUrl url(u"qrc:/Main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}