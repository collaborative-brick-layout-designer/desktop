// bld-import-server: the website's LDraw / Studio / LDD import, headless
// (docs/import-server.md). The server runs it once per step:
//
//   bld-import-server [limits] <request.json>   analyse or convert, as the
//                                               request says; the result
//                                               JSON goes to stdout
//   bld-import-server --version                 "bld-import-server X.Y.Z"
//   bld-import-server [limits] --sandbox-check  what the lock-down blocks
//
// limits: --cpu <seconds> --memory-mb <MB> --file-size-mb <MB> --open-files <n>
//
// The sandbox goes up first, before Qt starts or any file is read.

#include "importserver/ImportServer.h"
#include "importserver/Sandbox.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef BLD_VERSION
#define BLD_VERSION "0.0.0"
#endif

using namespace bld;

namespace {

void print(const QJsonObject& o) {
    const QByteArray out = QJsonDocument(o).toJson(QJsonDocument::Compact);
    std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

int usage() {
    std::fprintf(stderr,
                 "usage: bld-import-server [--cpu S] [--memory-mb MB] [--file-size-mb MB] [--open-files N] "
                 "<request.json> | --sandbox-check | --version\n");
    return 2;
}

bool positiveInt(const char* text, int* out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (!end || *end || v <= 0 || v > 1 << 20) return false;
    *out = static_cast<int>(v);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    importserver::SandboxLimits limits;
    const char* requestPath = nullptr;
    bool sandboxCheck = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&](int* out) {
            if (i + 1 >= argc) return false;
            ++i;
            return positiveInt(argv[i], out);
        };
        if (a == "--version") {
            std::printf("bld-import-server %s\n", BLD_VERSION);
            return 0;
        } else if (a == "--sandbox-check") {
            sandboxCheck = true;
        } else if (a == "--cpu") {
            if (!value(&limits.cpuSeconds)) return usage();
        } else if (a == "--memory-mb") {
            if (!value(&limits.memoryMb)) return usage();
        } else if (a == "--file-size-mb") {
            if (!value(&limits.fileSizeMb)) return usage();
        } else if (a == "--open-files") {
            if (!value(&limits.openFiles)) return usage();
        } else if (!requestPath && a.rfind("--", 0) != 0) {
            requestPath = argv[i];
        } else {
            return usage();
        }
    }
    if (!sandboxCheck && !requestPath) return usage();

    // Before Qt: unshare needs a process with one thread.
    QJsonObject sandbox = importserver::enterSandbox(limits);

    if (sandboxCheck) {
        sandbox.insert(QStringLiteral("socketBlocked"), !importserver::canOpenSocket());
#if defined(__linux__)
        char* const args[] = { const_cast<char*>("/bin/true"), nullptr };
        sandbox.insert(QStringLiteral("execBlocked"), execv("/bin/true", args) != 0);
        const pid_t pid = fork();
        if (pid == 0) _exit(0);
        if (pid > 0) waitpid(pid, nullptr, 0);
        sandbox.insert(QStringLiteral("forkBlocked"), pid < 0);
#endif
        print(QJsonObject{ { QStringLiteral("ok"), true }, { QStringLiteral("sandbox"), sandbox } });
        return 0;
    }

    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Pictures are drawn with QGraphicsScene when there's no LDraw library,
    // as in the app, which needs a QApplication.
    QApplication app(argc, argv);

    QFile f(QString::fromLocal8Bit(requestPath));
    QJsonObject result;
    importserver::Request request;
    QString error;
    QJsonParseError parse{};
    // The request is the server's own (a few KB); anything bigger is wrong.
    const QByteArray bytes = f.open(QIODevice::ReadOnly) ? f.read(1 << 20) : QByteArray();
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (bytes.isEmpty() || !doc.isObject()) {
        result = { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("bad_request") },
                   { QStringLiteral("message"), QStringLiteral("The request couldn't be read.") } };
    } else if (!importserver::parseRequest(doc.object(), &request, &error)) {
        result = { { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("bad_request") },
                   { QStringLiteral("message"), error } };
    } else {
        result = importserver::run(request);
    }
    result.insert(QStringLiteral("version"), QStringLiteral(BLD_VERSION));
    result.insert(QStringLiteral("sandbox"), sandbox);
    print(result);
    return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}
