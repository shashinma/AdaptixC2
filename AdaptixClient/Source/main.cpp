#include <main.h>
#include <MainAdaptix.h>

#include <QByteArray>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QSslSocket>

MainAdaptix* GlobalClient = nullptr;

static QtMessageHandler defaultHandler = nullptr;

void messageFilter(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (msg.contains("invalid nullptr parameter"))
        return;
    if (msg.contains("Creating a fake screen"))
        return;
    if (msg.contains("mapTo(): parent must be in parent hierarchy"))
        return;
    if (msg.contains("wildcard call disconnects from destroyed signal"))
        return;

    if (defaultHandler)
        defaultHandler(type, context, msg);
}

int main(int argc, char *argv[])
{
    defaultHandler = qInstallMessageHandler(messageFilter);

    // Teamserver uses self-signed TLS; Qt WebEngine often still logs cert failures even after QWebEnginePage::certificateError.
    // Default: append --ignore-certificate-errors when WebEngine is linked. Opt out: ADAPTIX_STRICT_WEBENGINE_TLS=1
#if defined(HAS_QT_WEBENGINE)
    if (qEnvironmentVariableIntValue("ADAPTIX_STRICT_WEBENGINE_TLS") != 1) {
        QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        const QByteArray ignore = QByteArrayLiteral("--ignore-certificate-errors");
        if (!flags.contains(ignore)) {
            if (!flags.isEmpty())
                flags += ' ';
            flags += ignore;
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
        }
    }
#endif

    QLoggingCategory::setFilterRules(
        "qt.text.font.db=false\n"
        "qt.text.font.db.debug=false\n"
        "qt.text.font.db.warning=false\n"
        "qt.text.font.db.info=false\n"
        "qt.text.font.db.critical=false\n"
        "qt.core.qobject.connect=false"
    );

    QApplication a(argc, argv);

    // Force early SSL backend initialization
    QSslSocket::supportsSsl();

    a.setQuitOnLastWindowClosed(true);

    GlobalClient = new MainAdaptix();
    GlobalClient->Start();

    delete GlobalClient;
    GlobalClient = nullptr;

    return 0;
}
