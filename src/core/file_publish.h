#pragma once

#include <QString>

namespace oidm {

// Publishes an already closed temporary file into its final location without
// replacing an existing regular file or symlink. Both paths must share a volume.
// If publication fails, the temporary file is kept for recovery.
[[nodiscard]] bool publishFileNoReplace(const QString &temporaryPath,
                                        const QString &destinationPath,
                                        QString *error = nullptr);

} // namespace oidm
