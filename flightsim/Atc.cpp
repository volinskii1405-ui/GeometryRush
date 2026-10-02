#include "Atc.h"
#include "Autopilot.h"
#include "Environment.h"
#include "Missions.h"
#include "UiText.h"
#include "World.h"

using namespace fs;
using ui::L;

namespace {
float Sc() { return fmaxf(fminf(GetScreenHeight() / 900.0f, GetScreenWidth() / 1500.0f), 0.5f); }

const char* Tower(int airport)
{
    return world::GetAirport(airport).main ? L("Island Tower", "Остров-Вышка") : L("Pass Radio", "Перевал-Радио");
}
const char* ApproachName() { return L("Island Approach", "Остров-Подход"); }
} // namespace

const char* Atc::Callsign(const Aircraft& a) const
{
    switch (a.Type().kind) {
    case AircraftKind::Prop: return "RA-1725";
    case AircraftKind::Airliner: return L("Aero 512", "Аэро 512");
    default: return "RA-67217";
    }
}

void Atc::Start(const FlightSetup& s, const Environment& env, bool microburst)
{
    msgs_.clear();
    newMsg_ = false;
    time_ = 0;
    freeFlight_ = s.mission < 0;
    runwayStart_ = s.start == StartKind::Runway;
    microburst_ = microburst;
    startAirport_ = s.airport;
    takeoffCleared_ = departed_ = handedOff_ = false;
    approachAirport_ = -1;
    approachEnd_ = 0;
    landingCleared_ = occupiedWarned_ = goAroundOrdered_ = landedMsg_ = false;
    lastLiftoff_ = -1;
    nextQnhChange_ = (float)GetRandomValue(600, 900);
    trafficOn_ = trafficRolling_ = false;
    trafficSpeed_ = 0;
    finalCall_ = false;
    // На заходе с самого начала — уже на связи с Подходом.
    if (s.start == StartKind::Final) {
        approachAirport_ = s.airport;
        approachEnd_ = s.end;
        departed_ = handedOff_ = true;
        finalCall_ = true;
    } else if (s.start == StartKind::Air) {
        departed_ = handedOff_ = true;
    }
    (void)env;
}

void Atc::Say(const std::string& text)
{
    msgs_.push_back({text, 0.0f});
    if (msgs_.size() > 4) msgs_.erase(msgs_.begin());
    newMsg_ = true;
}

// С какой стороны заходит самолёт: торец, перед порогом которого он находится.
int Atc::ApproachEnd(int airport, Vector3 pos) const
{
    const Airport& port = world::GetAirport(airport);
    if (!port.main) return 0;   // на «Перевал» — только 16
    Vector3 d = port.rwy.Dir(0);
    float along = (pos.x - port.rwy.center.x) * d.x + (pos.z - port.rwy.center.z) * d.z;
    return along < 0 ? 0 : 1;
}

bool Atc::Update(Aircraft& a, const Autopilot& ap, Environment& env, float radioAltFt, const char* windName, float dt)
{
    newMsg_ = false;
    time_ += dt;
    for (Msg& m : msgs_) m.age += dt;
    if (a.crashed) return false;
    const char* cs = Callsign(a);
    const bool airborne = !a.onGround && radioAltFt > 50.0f;
    const float gs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z);

    // ---- взлёт
    if (runwayStart_ && !takeoffCleared_ && time_ > 1.5f) {
        takeoffCleared_ = true;
        const Runway& rw = world::GetAirport(startAirport_).rwy;
        int end = ApproachEnd(startAirport_, a.pos);
        if (world::GetAirport(startAirport_).main)
            Say(TextFormat(L("%s: %s, wind %s, runway %s, QNH %d, cleared for takeoff.", "%s: %s, ветер %s, ВПП %s, QNH %d, взлёт разрешаю."),
                           Tower(startAirport_), cs, windName, rw.ident[end], (int)roundf(env.qnh)));
        else
            Say(TextFormat(L("%s: %s, runway %s clear, wind %s, QNH %d, take off at your discretion.",
                             "%s: %s, полоса %s свободна, ветер %s, QNH %d, взлёт на ваше усмотрение."),
                           Tower(startAirport_), cs, rw.ident[end], windName, (int)roundf(env.qnh)));
    }
    if (airborne && !departed_) {
        departed_ = true;
        lastLiftoff_ = time_;
    }
    if (departed_ && !handedOff_ && radioAltFt > 1000.0f) {
        handedOff_ = true;
        Say(TextFormat(L("%s: %s, climb %d ft, contact Approach. Good day.", "%s: %s, набирайте %d ft, работайте с Подходом. До свидания."),
                       Tower(startAirport_), cs, (int)ap.altTarget));
    }

    // Старт на заходе: Подход уже ведёт нас на ILS.
    if (finalCall_ && time_ > 2.0f) {
        finalCall_ = false;
        const Runway& rw = world::GetAirport(approachAirport_).rwy;
        Say(TextFormat(L("%s: %s, cleared %s approach runway %s, wind %s, QNH %d.", "%s: %s, заход %s на ВПП %s разрешаю, ветер %s, QNH %d."),
                       ApproachName(), cs, rw.ils ? "ILS" : L("visual", "визуальный"), rw.ident[approachEnd_], windName, (int)roundf(env.qnh)));
        if (microburst_)
            Say(L("Caution: windshear reported on final, loss of 30 knots.", "Внимание: на глиссаде сообщают о сдвиге ветра, потеря скорости до 30 узлов."));
    }

    // ---- давление меняется: Подход сообщает новое
    if (time_ > nextQnhChange_ && airborne) {
        nextQnhChange_ = time_ + (float)GetRandomValue(600, 900);
        float delta = (float)GetRandomValue(2, 4) * (GetRandomValue(0, 1) ? 1.0f : -1.0f);
        env.qnh = Clampf(env.qnh + delta, 985.0f, 1035.0f);
        Say(TextFormat(L("%s: %s, QNH now %d.", "%s: %s, давление сменилось: QNH %d."), ApproachName(), cs, (int)roundf(env.qnh)));
    }

    // ---- заход: ближайший аэродром в 18 км, снижаемся или уже низко
    float distAp = 0;
    int near = world::NearestAirport(a.pos.x, a.pos.z, &distAp);
    bool afterTakeoff = lastLiftoff_ < 0 || time_ - lastLiftoff_ > 90.0f;
    if (airborne && afterTakeoff && near != approachAirport_ && distAp < 18000.0f && distAp > 3000.0f &&
        (a.vel.y * MS_TO_FPM < -300.0f || radioAltFt < 3500.0f)) {
        approachAirport_ = near;
        approachEnd_ = ApproachEnd(near, a.pos);
        landingCleared_ = occupiedWarned_ = goAroundOrdered_ = landedMsg_ = false;
        const Runway& rw = world::GetAirport(near).rwy;
        if (world::GetAirport(near).main)
            Say(TextFormat(L("%s: %s, descend 2000 ft, QNH %d, expect ILS approach runway %s.",
                             "%s: %s, снижайтесь 2000 ft, QNH %d, ожидайте заход по ILS на ВПП %s."),
                           ApproachName(), cs, (int)roundf(env.qnh), rw.ident[approachEnd_]));
        else
            Say(TextFormat(L("%s: %s, QNH %d, visual approach runway %s, PAPI on the left.",
                             "%s: %s, QNH %d, визуальный заход на ВПП %s, PAPI слева."),
                           Tower(near), cs, (int)roundf(env.qnh), rw.ident[approachEnd_]));
        if (microburst_)
            Say(L("Caution: windshear reported on final, loss of 30 knots.", "Внимание: на глиссаде сообщают о сдвиге ветра, потеря скорости до 30 узлов."));
        // Иногда в свободном полёте полосу занимает другой самолёт.
        if (freeFlight_ && world::GetAirport(near).main && GetRandomValue(0, 99) < 20) {
            Vector3 d = rw.Dir(approachEnd_);
            Vector3 p = Vector3Add(rw.Threshold(approachEnd_), Vector3Scale(d, 450.0f));
            Controls c;
            traffic_.SetType(AircraftKind::Prop);
            traffic_.Reset({p.x, rw.center.y + traffic_.Type().restHeight, p.z}, rw.Course(approachEnd_), 0.0f, true, c);
            traffic_.engines[0].n1 = 70.0f;
            trafficDir_ = d;
            trafficOn_ = true;
            trafficRolling_ = false;
            trafficSpeed_ = 0;
        }
    }

    // ---- финальная прямая
    if (approachAirport_ >= 0 && airborne && a.gear > 0.99f) {
        const Runway& rw = world::GetAirport(approachAirport_).rwy;
        Vector3 d = rw.Dir(approachEnd_), th = rw.Threshold(approachEnd_);
        float toThr = (th.x - a.pos.x) * d.x + (th.z - a.pos.z) * d.z;
        float lateral = fabsf((a.pos.x - th.x) * -d.z + (a.pos.z - th.z) * d.x);
        bool aligned = fabsf(WrapDeg180(a.HeadingDeg() - rw.Course(approachEnd_))) < 35.0f && lateral < 800.0f;
        if (aligned && toThr > 0.0f && toThr < 9000.0f) {
            if (trafficOn_ && !trafficRolling_) {
                if (!occupiedWarned_) {
                    occupiedWarned_ = true;
                    Say(TextFormat(L("%s: %s, continue approach, traffic lining up on the runway.", "%s: %s, продолжайте заход, на полосе самолёт на исполнительном."),
                                   Tower(approachAirport_), cs));
                }
                if (toThr < 3200.0f && !goAroundOrdered_) {
                    goAroundOrdered_ = true;
                    trafficRolling_ = true;   // всё-таки взлетает перед вами
                    Say(TextFormat(L("%s: %s, GO AROUND, runway occupied! Climb 2000 ft.", "%s: %s, УХОДИТЕ НА ВТОРОЙ КРУГ, полоса занята! Набор 2000 ft."),
                                   Tower(approachAirport_), cs));
                }
            } else if (!landingCleared_ && !trafficOn_) {
                landingCleared_ = true;
                Say(TextFormat(L("%s: %s, wind %s, runway %s, cleared to land.", "%s: %s, ветер %s, ВПП %s, посадку разрешаю."),
                               Tower(approachAirport_), cs, windName, rw.ident[approachEnd_]));
            }
        }
    }
    // После приземления и остановки — освободить полосу.
    if (approachAirport_ >= 0 && a.onGround && gs < 12.0f && !landedMsg_ && landingCleared_) {
        landedMsg_ = true;
        Say(TextFormat(L("%s: %s, landed. Vacate the runway, taxi to the apron.", "%s: %s, посадка. Освобождайте полосу, рулите на перрон."),
                       Tower(approachAirport_), cs));
    }

    // ---- самолёт на полосе: разбегается и улетает после команды на уход
    if (trafficOn_) {
        if (trafficRolling_) {
            trafficSpeed_ = fminf(trafficSpeed_ + 2.5f * dt, 36.0f);
            Vector3 v = Vector3Scale(trafficDir_, trafficSpeed_);
            if (trafficSpeed_ > 28.0f) v.y = 3.0f;
            traffic_.pos = Vector3Add(traffic_.pos, Vector3Scale(v, dt));
            traffic_.vel = v;
            const Runway& rw = world::GetAirport(approachAirport_ < 0 ? 0 : approachAirport_).rwy;
            if (Vector3Distance(traffic_.pos, rw.center) > 6000.0f) {
                trafficOn_ = false;
                landingCleared_ = false;
                occupiedWarned_ = false;
                Say(TextFormat(L("%s: %s, runway now clear, report final.", "%s: %s, полоса освободилась, доложите на прямой."),
                               Tower(approachAirport_ < 0 ? 0 : approachAirport_), cs));
            }
        }
        // Сели на занятую полосу — столкновение.
        Vector3 dlt = Vector3Subtract(a.pos, traffic_.pos);
        if (sqrtf(dlt.x * dlt.x + dlt.z * dlt.z) < a.Type().span * 0.5f + 6.0f && fabsf(dlt.y) < 5.0f)
            a.ForceCrash(L("Collision with the aircraft on the runway", "Столкновение с самолётом на полосе"));
    }
    return newMsg_;
}

void Atc::Draw() const
{
    const float S = Sc();
    const float sh = (float)GetScreenHeight();
    const float panelTop = sh - 300.0f * S;
    const float w = 560.0f * S, size = 14.0f * S, lineH = 18.0f * S;
    // Строки с переносом по словам.
    struct Line { std::string s; unsigned char alpha; bool head; };
    std::vector<Line> lines;
    for (const Msg& m : msgs_) {
        if (m.age > 16.0f) continue;
        unsigned char alpha = (unsigned char)(255.0f * Clampf((16.0f - m.age) / 3.0f, 0.0f, 1.0f));
        std::string cur, word;
        bool head = true;
        auto flush = [&]() { lines.push_back({cur, alpha, head}); head = false; cur.clear(); };
        for (size_t i = 0; i <= m.text.size(); ++i) {
            char ch = i < m.text.size() ? m.text[i] : ' ';
            if (ch == ' ') {
                std::string test = cur.empty() ? word : cur + " " + word;
                if (ui::Measure(test.c_str(), size) > w - 16 * S && !cur.empty()) {
                    flush();
                    cur = word;
                } else {
                    cur = test;
                }
                word.clear();
            } else {
                word += ch;
            }
        }
        if (!cur.empty()) flush();
    }
    if (lines.empty()) return;
    float h = lines.size() * lineH + 26 * S;
    float x = 14 * S, y = panelTop - 36 * S - h;
    DrawRectangle((int)x, (int)y, (int)w, (int)h, Color{0, 0, 0, 150});
    DrawRectangle((int)x, (int)y, (int)(3 * S), (int)h, Color{90, 200, 120, 220});
    ui::Draw(L("RADIO", "РАДИО"), x + 10 * S, y + 4 * S, 12 * S, Color{90, 200, 120, 255}, true);
    float ly = y + 22 * S;
    for (size_t i = 0; i < lines.size(); ++i) {
        Color c{235, 235, 220, lines[i].alpha};
        ui::Draw(lines[i].s.c_str(), x + 10 * S, ly, size, c);
        ly += lineH;
    }
}
