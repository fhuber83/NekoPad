#pragma once

#include <QIcon>

namespace neko::pad::ui {

/// The application icon (the cat on the notepad), at every size that is compiled in.
/// The source is `resources/nekopad.svg`; the PNGs beside it and `resources/nekopad.ico`
/// for the Windows executable are rendered from it (`scripts/render-icons.sh`).
[[nodiscard]] QIcon appIcon();

} // namespace neko::pad::ui
