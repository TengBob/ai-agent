#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtWebEngineQuick/QtWebEngineQuick>

#include "database/DatabaseManager.h"
#include "services/AgentManager.h"
#include "chat/ChatEngine.h"
#include "chat/GroupChatEngine.h"
#include "chat/ComparisonEngine.h"
#include "services/TemplateStore.h"
#include "services/Tracker.h"
#include "services/PdfConverter.h"
#include "tools/ToolExecutor.h"
#include "models/AgentListModel.h"

int main(int argc, char *argv[])
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--allow-file-access-from-files");
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    app.setOrganizationName("AgentCreator");
    app.setApplicationName("AgentCreator");

    QQuickStyle::setStyle("Material");

    if (!DatabaseManager::instance().initialize()) {
        qFatal("Failed to initialize database");
    }

    qmlRegisterUncreatableType<AgentListModel>("AgentCreator", 1, 0,
        "AgentListModel", "Use agentManager.agentListModel");

    auto *agentManager  = new AgentManager();
    auto *chatEngine    = new ChatEngine();
    auto *groupChat         = new GroupChatEngine();
    auto *comparisonEngine  = new ComparisonEngine();
    auto *templateStore = new TemplateStore();
    auto *tracker       = new Tracker();
    auto *pdfConverter  = new PdfConverter();
    ToolExecutor::setPdfConverter(pdfConverter);
    ChatEngine::setPdfConverter(pdfConverter);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("agentManager",  agentManager);
    engine.rootContext()->setContextProperty("chatEngine",    chatEngine);
    engine.rootContext()->setContextProperty("groupChat",          groupChat);
    engine.rootContext()->setContextProperty("comparisonEngine",   comparisonEngine);
    engine.rootContext()->setContextProperty("templateStore", templateStore);
    engine.rootContext()->setContextProperty("tracker",       tracker);
    engine.rootContext()->setContextProperty("pdfConverter",  pdfConverter);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("stock_project", "Main");
    return QGuiApplication::exec();
}
