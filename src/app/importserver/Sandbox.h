#pragma once

// The converter's own lock-down, applied in main() before Qt starts and
// before the uploaded file is opened (docs/import-server.md, "Sandbox"):
//
//   rlimits   CPU seconds, address space, file size, open files, no core
//             files; soft = hard, so they can't be raised again
//   netns     an empty network namespace (unshare CLONE_NEWUSER|NEWNET),
//             when the host allows it; Docker's default profile doesn't
//   seccomp   always on Linux x86_64 / aarch64: socket() fails (no network
//             whatever the namespace), no new programs (execve), no new
//             processes (clone without CLONE_THREAD, fork, clone3), and no
//             ptrace, mount, namespaces, bpf, keyrings or io_uring
//
// The server adds the wall-clock timeout, the empty environment and the
// job's own temporary folder.

#include <QJsonObject>

namespace bld::importserver {

struct SandboxLimits {
    int cpuSeconds = 120;
    int memoryMb = 4096;     // address space; LDD's Assets.lif (~500 MB) is mapped
    int fileSizeMb = 256;    // any one file written
    int openFiles = 256;
};

// Applies everything it can; the result says what took ("rlimits",
// "noNewPrivs", "netns", "seccomp", each true or false, and the limits).
// Must run while the process has one thread.
QJsonObject enterSandbox(const SandboxLimits& limits);

// Built with AddressSanitizer (tests only): its shadow memory needs far more
// address space than any limit, so RLIMIT_AS is left alone there.
bool builtWithAsan();

// After enterSandbox: whether a TCP socket can still be made (tests).
bool canOpenSocket();

}  // namespace bld::importserver
