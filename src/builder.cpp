#include "builder.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

namespace builder {
namespace {

std::atomic<State> g_state{State::Idle};
std::mutex g_mutex;
std::string g_log, g_out;

void append(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_log += s;
}

// Roda um comando e junta a saída no log; true se terminou sem erro.
bool run(const std::string& cmd) {
    append("$ " + cmd + "\n");
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return false;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), p)) append(buf);
    return pclose(p) == 0;
}

} // namespace

void start(const std::string& root) {
    if (g_state == State::Running) return;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_log.clear();
        g_out = root + "/Jogo-final/ga";
    }
    g_state = State::Running;
    std::thread([root]() {
        const std::string q = "'" + root + "'", dir = "'" + root + "/build-final'";
        bool ok = run("cmake -S " + q + " -B " + dir + " -DCMAKE_BUILD_TYPE=Release -DGA_EDITOR=OFF") &&
                  run("cmake --build " + dir + " -j$(nproc)");
        // cópia do jogo: executável + shaders e modelos, sem código e sem as pastas de compilação
        const std::string pkg = "'" + root + "/Jogo-final'";
        ok = ok && run("mkdir -p " + pkg) &&
             run("rsync -a --delete --exclude /build --exclude /build-final --exclude /Jogo-final --exclude /src "
                 "--exclude /third_party --exclude /.git --exclude /.vscode --exclude '*.md' --exclude /CMakeLists.txt "
                 "--exclude /run.sh --exclude /index.html " + q + "/ " + pkg + "/") &&
             run("cp " + dir + "/ga " + pkg + "/ga");
        append(ok ? "\nPronto.\n" : "\nFalhou.\n");
        g_state = ok ? State::Ok : State::Failed;
    }).detach();
}

State state() { return g_state; }

std::string log() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_log;
}

std::string outputPath() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_out;
}

} // namespace builder
