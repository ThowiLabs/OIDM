#include "core/file_publish.h"

#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#elif defined(Q_OS_LINUX)
#  include <cerrno>
#  include <cstring>
#  include <fcntl.h>
#  include <linux/fs.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#elif defined(Q_OS_UNIX)
#  include <cerrno>
#  include <cstring>
#  include <unistd.h>
#endif

namespace oidm {

bool publishFileNoReplace(const QString &temporaryPath, const QString &destinationPath,
                          QString *error)
{
    if (temporaryPath.isEmpty() || destinationPath.isEmpty()
        || temporaryPath == destinationPath || QFileInfo(temporaryPath).isSymLink()) {
        if (error) *error = QStringLiteral("Ruta temporal o destino inválido.");
        return false;
    }

#if defined(Q_OS_WIN)
    // MOVEFILE_REPLACE_EXISTING is intentionally NOT passed.
    const BOOL ok = MoveFileExW(reinterpret_cast<LPCWSTR>(temporaryPath.utf16()),
                                reinterpret_cast<LPCWSTR>(destinationPath.utf16()),
                                MOVEFILE_WRITE_THROUGH);
    if (!ok && error) {
        *error = QStringLiteral("Windows no pudo publicar sin reemplazar (código %1).")
            .arg(static_cast<unsigned long>(GetLastError()));
    }
    return ok != 0;
#elif defined(Q_OS_LINUX)
    const QByteArray source = QFile::encodeName(temporaryPath);
    const QByteArray destination = QFile::encodeName(destinationPath);
    // Atomic no-replace rename; unlike rename(2), an attacker cannot insert a
    // destination between a userspace existence check and the rename.
    if (::syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD,
                  destination.constData(), RENAME_NOREPLACE) == 0) return true;
    const int renameError = errno;
    if (renameError != ENOSYS && renameError != EINVAL) {
        if (error) *error = QString::fromLocal8Bit(std::strerror(renameError));
        return false;
    }
    // Older kernels / filesystems: hard-link is exclusive and atomic on the
    // same volume, then remove the temporary name after successful linking.
    if (::link(source.constData(), destination.constData()) != 0) {
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    (void)::unlink(source.constData());
    return true;
#elif defined(Q_OS_UNIX)
    const QByteArray source = QFile::encodeName(temporaryPath);
    const QByteArray destination = QFile::encodeName(destinationPath);
    if (::link(source.constData(), destination.constData()) != 0) {
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    (void)::unlink(source.constData());
    return true;
#else
    const bool ok = QFile::rename(temporaryPath, destinationPath);
    if (!ok && error) *error = QStringLiteral("No se pudo publicar sin reemplazar el destino.");
    return ok;
#endif
}

} // namespace oidm
