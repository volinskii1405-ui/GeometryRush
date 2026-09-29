// level_check — консольная проверка проходимости уровня.
// Перебирает (BFS) все варианты "держать / отпустить" с шагом решения
// 1/60 с (и 1/30 с — проверка, что уровень не требует покадровой точности),
// используя ту же физику, что и игра. Окно не открывается.
//
//   level_check levels/01_neon_steps.txt [--step 4] [--path]
#include "Level.h"
#include "Player.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

struct Node {
    Player p;
    bool   prevHeld;
    int    parent;   // индекс в предыдущем слое
    bool   held;     // решение, которое привело сюда
};

static unsigned long long KeyOf(const Node& n) {
    const Player& p = n.p;
    long long y  = std::llround(p.pos.y * 2.0f);
    long long vy = std::llround(p.vy / 5.0f);
    int used = 0;
    for (unsigned char u : p.portalUsed) used += u;
    unsigned long long k = (unsigned long long)(y & 0xFFFFF);
    k = k * 1000003ULL + (unsigned long long)(vy & 0xFFFFF);
    k = k * 31 + (int)p.mode;
    k = k * 7 + p.gravityFlipped * 4 + p.onGround * 2 + p.wantFlip;
    k = k * 3 + n.prevHeld;
    k = k * 131 + used;
    return k;
}

struct Result { bool ok; float best; std::vector<bool> path; };

// Прогон с постоянным вводом: уровень не должен проходиться "без рук".
static float RunConstant(const Level& level, bool held) {
    Player p;
    p.Reset(level);
    bool first = true;
    while (!p.dead && !p.finished) {
        p.Step(level, held, held && first, nullptr);
        first = false;
    }
    return p.finished ? 1.0f : level.Progress(p.pos.x);
}

static Result Solve(const Level& level, int stepsPerDecision, size_t cap) {
    std::vector<std::vector<std::pair<int, bool>>> history;   // parent, held
    std::vector<Node> layer(1);
    layer[0].p.Reset(level);
    layer[0].prevHeld = false;
    layer[0].parent = -1;
    layer[0].held = false;
    float best = 0;

    while (!layer.empty()) {
        std::vector<Node> next;
        std::unordered_set<unsigned long long> seen;
        for (int i = 0; i < (int)layer.size(); ++i) {
            for (int h = 0; h < 2; ++h) {
                Node n = layer[i];
                bool held = h == 1;
                bool pressed = held && !n.prevHeld;
                for (int s = 0; s < stepsPerDecision && !n.p.dead && !n.p.finished; ++s) {
                    n.p.Step(level, held, pressed && s == 0, nullptr);
                }
                n.prevHeld = held;
                n.parent = i;
                n.held = held;
                best = std::max(best, level.Progress(n.p.pos.x));
                if (n.p.dead) continue;
                if (n.p.finished) {
                    std::vector<bool> path{held};
                    int idx = i;
                    for (int l = (int)history.size() - 1; l >= 0 && idx >= 0; --l) {
                        path.push_back(history[l][idx].second);
                        idx = history[l][idx].first;
                    }
                    std::vector<bool> fwd(path.rbegin(), path.rend());
                    return {true, 1.0f, fwd};
                }
                if (seen.insert(KeyOf(n)).second) next.push_back(std::move(n));
            }
        }
        if (next.size() > cap) {                // равномерное прореживание
            std::vector<Node> thin;
            double stride = (double)next.size() / cap;
            for (size_t k = 0; k < cap; ++k) thin.push_back(std::move(next[(size_t)(k * stride)]));
            next.swap(thin);
        }
        std::vector<std::pair<int, bool>> hist;
        for (auto& n : next) hist.push_back({n.parent, n.held});
        history.push_back(std::move(hist));
        layer.swap(next);
    }
    return {false, best, {}};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: level_check <level.txt> [--path]\n");
        return 2;
    }
    SetTraceLogLevel(LOG_WARNING);
    bool printPath = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--path")) printPath = true;
        else files.push_back(argv[i]);
    }
    int failures = 0;
    for (const std::string& f : files) {
        Level level;
        std::string err;
        if (!level.LoadFromFile(f, &err)) {
            std::printf("%s: LOAD ERROR %s\n", f.c_str(), err.c_str());
            ++failures;
            continue;
        }
        Result r60 = Solve(level, 4, 20000);
        Result r30 = Solve(level, 8, 20000);
        std::printf("%-40s '%s'  60Hz: %s (best %.1f%%)   30Hz: %s (best %.1f%%)\n", f.c_str(),
                    level.Name().c_str(), r60.ok ? "PASS" : "FAIL", r60.best * 100,
                    r30.ok ? "PASS" : "FAIL", r30.best * 100);
        std::printf("    trivial runs: never press %.1f%%, always hold %.1f%%\n",
                    RunConstant(level, false) * 100, RunConstant(level, true) * 100);
        if (!r60.ok) ++failures;
        if (printPath && r30.ok) {
            std::printf("path(30Hz): ");
            for (bool b : r30.path) std::putchar(b ? '1' : '0');
            std::printf("\n");
        }
    }
    return failures ? 1 : 0;
}
