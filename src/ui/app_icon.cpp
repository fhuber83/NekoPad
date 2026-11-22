#include "ui/app_icon.hpp"

#include <array>

/// nekopad.qrc is compiled into the static nekopad_ui, whose resource initializer the
/// linker drops unless something refers to it. Q_INIT_RESOURCE must be called outside
/// any namespace, an anonymous one included.
static void initResources() {
    static const bool done = [] {
        Q_INIT_RESOURCE(nekopad);
        return true;
    }();
    (void)done;
}

namespace neko::pad::ui {

QIcon appIcon() {
    ::initResources();
    static constexpr std::array sizes{16, 24, 32, 48, 64, 128, 256, 512};
    QIcon icon;
    for (const int size : sizes) {
        icon.addFile(QStringLiteral(":/nekopad/icons/app/nekopad_%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

} // namespace neko::pad::ui
