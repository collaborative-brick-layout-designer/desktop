#include "Sandbox.h"

#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bld::importserver {

#if defined(__linux__)
namespace {

bool setLimit(int resource, rlim_t value) {
    const rlimit r{ value, value };
    return setrlimit(resource, &r) == 0;
}

#if defined(__x86_64__)
constexpr unsigned kArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr unsigned kArch = AUDIT_ARCH_AARCH64;
#endif

#if defined(__x86_64__) || defined(__aarch64__)
// Classic BPF: load the syscall number, compare, return.
#define BLD_LOAD(field) BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, field))
#define BLD_DENY(nr, err) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ((err) & SECCOMP_RET_DATA))

bool installSeccomp() {
    // clone's first argument holds its flags on both architectures.
    struct sock_filter filter[] = {
        BLD_LOAD(arch),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kArch, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BLD_LOAD(nr),
#if defined(__x86_64__)
        // The x32 ABI's numbers would get past the checks below.
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BLD_DENY(__NR_fork, EPERM),
        BLD_DENY(__NR_vfork, EPERM),
#endif
        // No network: no sockets at all (the server's pipes are inherited).
        BLD_DENY(__NR_socket, EACCES),
        BLD_DENY(__NR_execve, EPERM),
        BLD_DENY(__NR_execveat, EPERM),
        // glibc falls back to clone, whose flags we can see.
        BLD_DENY(__NR_clone3, ENOSYS),
        BLD_DENY(__NR_ptrace, EPERM),
        BLD_DENY(__NR_process_vm_writev, EPERM),
        BLD_DENY(__NR_process_vm_readv, EPERM),
        BLD_DENY(__NR_mount, EPERM),
        BLD_DENY(__NR_umount2, EPERM),
        BLD_DENY(__NR_unshare, EPERM),
        BLD_DENY(__NR_setns, EPERM),
        BLD_DENY(__NR_bpf, EPERM),
        BLD_DENY(__NR_perf_event_open, EPERM),
        BLD_DENY(__NR_keyctl, EPERM),
        BLD_DENY(__NR_add_key, EPERM),
        BLD_DENY(__NR_request_key, EPERM),
        BLD_DENY(__NR_userfaultfd, EPERM),
        BLD_DENY(__NR_io_uring_setup, EPERM),
        BLD_DENY(__NR_kexec_load, EPERM),
        BLD_DENY(__NR_init_module, EPERM),
        BLD_DENY(__NR_finit_module, EPERM),
        BLD_DENY(__NR_chroot, EPERM),
        BLD_DENY(__NR_pivot_root, EPERM),
        // clone: threads only (CLONE_THREAD), never a new process.
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_THREAD, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = { static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])), filter };
    return syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog) == 0;
}
#undef BLD_LOAD
#undef BLD_DENY
#else
bool installSeccomp() { return false; }
#endif

// An empty network namespace, as an unprivileged user namespace allows.
// Docker's default seccomp profile refuses it; then seccomp is the guard.
bool enterEmptyNetns() {
    const uid_t uid = getuid();
    const gid_t gid = getgid();
    if (unshare(CLONE_NEWUSER | CLONE_NEWNET) != 0) return false;
    // Map ourselves so files we write keep our ids.
    const auto write = [](const char* path, const QByteArray& text) {
        const int fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) return false;
        const bool ok = ::write(fd, text.constData(), static_cast<size_t>(text.size())) == text.size();
        close(fd);
        return ok;
    };
    write("/proc/self/setgroups", "deny");
    write("/proc/self/uid_map", QByteArray::number(uid) + ' ' + QByteArray::number(uid) + " 1");
    write("/proc/self/gid_map", QByteArray::number(gid) + ' ' + QByteArray::number(gid) + " 1");
    return true;
}

}  // namespace
#endif

QJsonObject enterSandbox(const SandboxLimits& limits) {
    QJsonObject out{ { QStringLiteral("cpuSeconds"), limits.cpuSeconds },
                     { QStringLiteral("memoryMb"), limits.memoryMb },
                     { QStringLiteral("fileSizeMb"), limits.fileSizeMb },
                     { QStringLiteral("openFiles"), limits.openFiles } };
#if defined(__linux__)
    const rlim_t mb = rlim_t(1) << 20;
    const bool limited = setLimit(RLIMIT_CPU, static_cast<rlim_t>(limits.cpuSeconds))
                      && setLimit(RLIMIT_AS, static_cast<rlim_t>(limits.memoryMb) * mb)
                      && setLimit(RLIMIT_FSIZE, static_cast<rlim_t>(limits.fileSizeMb) * mb)
                      && setLimit(RLIMIT_NOFILE, static_cast<rlim_t>(limits.openFiles))
                      && setLimit(RLIMIT_CORE, 0);
    out.insert(QStringLiteral("rlimits"), limited);
    const bool noNewPrivs = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0;
    out.insert(QStringLiteral("noNewPrivs"), noNewPrivs);
    out.insert(QStringLiteral("netns"), enterEmptyNetns());
    out.insert(QStringLiteral("seccomp"), noNewPrivs && installSeccomp());
#else
    out.insert(QStringLiteral("rlimits"), false);
    out.insert(QStringLiteral("noNewPrivs"), false);
    out.insert(QStringLiteral("netns"), false);
    out.insert(QStringLiteral("seccomp"), false);
#endif
    return out;
}

bool canOpenSocket() {
#if defined(__linux__)
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    close(fd);
    return true;
#else
    return true;
#endif
}

}  // namespace bld::importserver
