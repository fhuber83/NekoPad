#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

namespace neko::pad::ui {

/// "Open with Neko Pad" while Neko Pad is running opens the file in the running window,
/// as Notepad++ does: the second process hands its files over a local socket and
/// exits, which is also much faster than starting a second window.
///
/// The socket's name is per user (and per `key`, so tests do not meet the real
/// application).
class SingleInstance final : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// Hands `files` (absolute paths; may be empty, which just raises the window) to a
    /// running instance. True if one took them -- this process can quit.
    [[nodiscard]] bool sendToRunning(const QStringList& files, int timeoutMs = 500) const;

    /// Starts listening for later instances. False if another process listens already.
    bool listen();

    [[nodiscard]] QString serverName() const { return serverName_; }

signals:
    /// A later instance handed over `files`; the window should open them and come to
    /// the front.
    void filesReceived(const QStringList& files);

private:
    QString serverName_;
    QLocalServer* server_ = nullptr;
};

} // namespace neko::pad::ui
