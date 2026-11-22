#include "ui/app_icon.hpp"
#include "ui/main_window.hpp"
#include "ui/settings.hpp"
#include "ui/single_instance.hpp"

#include "neko/editor/editor_icons.hpp"
#include "neko/theme/theme_manager.hpp"
#include "neko/theme/themed_icon.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Neko"));
    QApplication::setApplicationName(QStringLiteral("Neko Pad"));
    QApplication::setApplicationVersion(QStringLiteral(NEKOPAD_VERSION_STRING));
    QApplication::setDesktopFileName(QStringLiteral("nekopad"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Neko Pad -- a fast text and source code editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption newInstance(
        {QStringLiteral("new-instance"), QStringLiteral("multiInst")},
        QStringLiteral("Open a new window even if Neko Pad is running already."));
    const QCommandLineOption noSession(
        QStringLiteral("no-session"), QStringLiteral("Do not reopen the files of the last session."));
    parser.addOption(newInstance);
    parser.addOption(noSession);
    parser.addPositionalArgument(
        QStringLiteral("files"), QStringLiteral("Files to open (file or file:line)."), QStringLiteral("[files...]"));
    parser.process(app);

    QStringList files;
    for (const QString& arg : parser.positionalArguments()) {
        // Absolute, because a running instance has a working directory of its own. Keep a
        // ":line" suffix as it is (absoluteFilePath leaves it alone).
        files << QFileInfo(arg).absoluteFilePath();
    }

    // The fast path: hand the files to the running window and quit before building any
    // UI of our own.
    const bool single = !parser.isSet(newInstance) && neko::pad::ui::settings::singleInstance();
    neko::pad::ui::SingleInstance instance(QStringLiteral("app"));
    if (single && instance.sendToRunning(files)) {
        return 0;
    }

    QApplication::setWindowIcon(neko::pad::ui::appIcon());

    // Look & Feel, as in Neko Grid and Neko Note: the saved widget style (Fusion if it is
    // gone), then the saved theme.
    auto& themes = neko::theme::ThemeManager::instance();
    if (!themes.setStyle(neko::theme::ThemeManager::savedStyle())) {
        themes.setStyle(QStringLiteral("Fusion"));
    }
    themes.setThemeMode(neko::theme::ThemeManager::savedThemeMode());

    // The editor library has no icons and stores nothing: hand it ours, and the saved font
    // and line wrapping, before the first editor exists.
    neko::editor::setEditorIconProvider(&neko::theme::themedIcon);
    neko::pad::ui::applyStoredEditorSettings();

    neko::pad::ui::MainWindow window;
    // --no-session neither reads the last session nor replaces it (nor its backups of
    // unsaved changes).
    window.setUseSession(!parser.isSet(noSession));
    if (!parser.isSet(noSession) && neko::pad::ui::settings::restoreSession()) {
        window.restoreSession();
    }
    window.openFiles(files);
    if (window.tabCount() == 0) {
        window.newDocument();
    }
    window.show();

    if (single) {
        instance.listen();
        QObject::connect(
            &instance, &neko::pad::ui::SingleInstance::filesReceived, &window, [&window](const QStringList& received) {
                window.openFiles(received);
                if (window.isMinimized()) {
                    window.showNormal();
                }
                window.raise();
                window.activateWindow();
            });
    }
    return QApplication::exec();
}
