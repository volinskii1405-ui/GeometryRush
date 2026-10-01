#include "Scenery.h"
#include "Terrain.h"
#include "World.h"

#include "rlgl.h"

#include <cstring>
#include <map>

using namespace fs;

namespace {

constexpr float CHUNK = 3000.0f;

// Сборщик геометрии: плоские треугольники с цветом вершин; при ~60 тыс. вершин начинает новый меш.
class MeshBuilder {
public:
    void Tri(Vector3 a, Vector3 b, Vector3 c, Color col)
    {
        if (pos_.size() / 3 + 3 > 60000) Flush();
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        for (Vector3 p : {a, b, c}) {
            pos_.insert(pos_.end(), {p.x, p.y, p.z});
            nrm_.insert(nrm_.end(), {n.x, n.y, n.z});
            col_.insert(col_.end(), {col.r, col.g, col.b, col.a});
        }
    }
    void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col)   // a b c d — против часовой стрелки
    {
        Tri(a, b, c, col);
        Tri(a, c, d, col);
    }
    void Flush()
    {
        if (pos_.empty()) return;
        Mesh m{};
        m.vertexCount = (int)(pos_.size() / 3);
        m.triangleCount = m.vertexCount / 3;
        m.vertices = (float*)MemAlloc((unsigned)(pos_.size() * sizeof(float)));
        m.normals = (float*)MemAlloc((unsigned)(nrm_.size() * sizeof(float)));
        m.colors = (unsigned char*)MemAlloc((unsigned)col_.size());
        memcpy(m.vertices, pos_.data(), pos_.size() * sizeof(float));
        memcpy(m.normals, nrm_.data(), nrm_.size() * sizeof(float));
        memcpy(m.colors, col_.data(), col_.size());
        UploadMesh(&m, false);
        out.push_back(m);
        pos_.clear();
        nrm_.clear();
        col_.clear();
    }
    std::vector<Mesh> out;

private:
    std::vector<float> pos_, nrm_;
    std::vector<unsigned char> col_;
};

unsigned g_rng = 2463534242u;
float Rand01()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (g_rng & 0xFFFFFF) / 16777216.0f;
}
float RandRange(float a, float b) { return a + (b - a) * Rand01(); }

Color Shade(Color c, float k)
{
    return Color{(unsigned char)Clampf(c.r * k, 0, 255), (unsigned char)Clampf(c.g * k, 0, 255),
                 (unsigned char)Clampf(c.b * k, 0, 255), c.a};
}

struct Builders {
    MeshBuilder solid, glow;
};

void AddTree(MeshBuilder& b, Vector3 base, float h, float r, Color c)
{
    const int sides = 6;
    Vector3 apex{base.x, base.y + h, base.z};
    float y0 = base.y + h * 0.22f;
    float rot = Rand01() * PI;
    for (int i = 0; i < sides; ++i) {
        float a0 = rot + 2 * PI * i / sides, a1 = rot + 2 * PI * (i + 1) / sides;
        Vector3 p0{base.x + cosf(a0) * r, y0, base.z + sinf(a0) * r};
        Vector3 p1{base.x + cosf(a1) * r, y0, base.z + sinf(a1) * r};
        b.Tri(apex, p1, p0, Shade(c, 0.85f + 0.3f * (i % 2)));
    }
    // Ствол — тонкая тёмная «призма» под кроной.
    Color trunk{78, 60, 44, 255};
    Vector3 t0{base.x - 0.3f, base.y, base.z}, t1{base.x + 0.3f, base.y, base.z};
    b.Quad(t0, t1, Vector3{t1.x, y0, t1.z}, Vector3{t0.x, y0, t0.z}, trunk);
}

// Дом: коробка с двускатной крышей. yaw — поворот вокруг вертикали.
void AddHouse(Builders& b, Vector3 base, float w, float d, float h, float yaw, Color wall, Color roof, bool lit)
{
    float cs = cosf(yaw), sn = sinf(yaw);
    auto P = [&](float x, float y, float z) { return Vector3{base.x + x * cs - z * sn, base.y + y, base.z + x * sn + z * cs}; };
    float hx = w * 0.5f, hz = d * 0.5f;
    Vector3 c[8] = {P(-hx, -1, -hz), P(hx, -1, -hz), P(hx, -1, hz), P(-hx, -1, hz),
                    P(-hx, h, -hz), P(hx, h, -hz), P(hx, h, hz), P(-hx, h, hz)};
    b.solid.Quad(c[0], c[4], c[5], c[1], wall);
    b.solid.Quad(c[1], c[5], c[6], c[2], Shade(wall, 0.9f));
    b.solid.Quad(c[2], c[6], c[7], c[3], wall);
    b.solid.Quad(c[3], c[7], c[4], c[0], Shade(wall, 0.9f));
    // Крыша
    float rh = h + w * 0.35f;
    Vector3 r0 = P(0, rh, -hz - 0.4f), r1 = P(0, rh, hz + 0.4f);
    Vector3 e0 = P(-hx - 0.4f, h - 0.2f, -hz - 0.4f), e1 = P(hx + 0.4f, h - 0.2f, -hz - 0.4f);
    Vector3 e2 = P(hx + 0.4f, h - 0.2f, hz + 0.4f), e3 = P(-hx - 0.4f, h - 0.2f, hz + 0.4f);
    b.solid.Quad(e0, e3, r1, r0, roof);
    b.solid.Quad(e1, r0, r1, e2, Shade(roof, 0.8f));
    b.solid.Tri(c[4], P(0, rh, -hz), c[5], wall);
    b.solid.Tri(c[6], P(0, rh, hz), c[7], wall);
    // Окна (светятся ночью)
    if (lit) {
        Color win{255, 214, 140, 255};
        for (int side = -1; side <= 1; side += 2) {
            float z = side * (hz + 0.05f);
            for (float x = -hx * 0.5f; x <= hx * 0.5f + 0.01f; x += hx) {
                Vector3 a = P(x - 0.7f, h * 0.35f, z), bb = P(x + 0.7f, h * 0.35f, z);
                Vector3 cc = P(x + 0.7f, h * 0.7f, z), dd = P(x - 0.7f, h * 0.7f, z);
                if (side > 0) b.glow.Quad(a, bb, cc, dd, win);
                else b.glow.Quad(bb, a, dd, cc, win);
            }
        }
    }
}

// Лента дороги, лежащая на рельефе.
void AddRoad(MeshBuilder& b, const Terrain& t, Vector3 a, Vector3 c, float width, Color col)
{
    Vector3 d{c.x - a.x, 0, c.z - a.z};
    float len = sqrtf(d.x * d.x + d.z * d.z);
    if (len < 1) return;
    d = Vector3Scale(d, 1.0f / len);
    Vector3 side{-d.z * width * 0.5f, 0, d.x * width * 0.5f};
    int steps = (int)(len / 20.0f) + 1;
    for (int i = 0; i < steps; ++i) {
        float s0 = len * i / steps, s1 = len * (i + 1) / steps;
        Vector3 p0{a.x + d.x * s0, 0, a.z + d.z * s0}, p1{a.x + d.x * s1, 0, a.z + d.z * s1};
        auto Y = [&](Vector3 p) { p.y = t.SurfaceHeight(p.x, p.z) + 0.45f; return p; };
        Vector3 q0 = Y(Vector3Subtract(p0, side)), q1 = Y(Vector3Add(p0, side));
        Vector3 q2 = Y(Vector3Add(p1, side)), q3 = Y(Vector3Subtract(p1, side));
        b.Quad(q0, q1, q2, q3, col);
    }
}

bool NearRunway(float x, float z, float margin)
{
    for (int i = 0; i < world::AirportCount(); ++i)
        if (world::GetAirport(i).rwy.Dist(x, z, margin, margin * 0.8f) <= 0.0f) return true;
    return false;
}

} // namespace

void Scenery::Build(const Terrain& t, Shader litShader)
{
    g_rng = 2463534242u;
    std::map<std::pair<int, int>, Builders> cells;
    auto cellOf = [](float x, float z) { return std::make_pair((int)floorf(x / CHUNK), (int)floorf(z / CHUNK)); };
    auto builders = [&](float x, float z) -> Builders& { return cells[cellOf(x, z)]; };

    // ---- дороги: от аэропорта в город, к горному аэродрому и в деревню на западе
    struct Road { std::vector<Vector3> pts; float width; };
    std::vector<Road> roads = {
        {{{0, 0, -265}, {300, 0, -900}, {1600, 0, -1700}}, 9},
        {{{1600, 0, -1700}, {3400, 0, -2900}, {5400, 0, -4300}, {7000, 0, -5600}, {8300, 0, -7000}, {8820, 0, -7480}}, 7},
        {{{-1500, 0, -230}, {-3200, 0, 600}, {-5200, 0, 1600}, {-6400, 0, 2300}}, 7},
    };
    std::vector<std::pair<Vector3, Vector3>> segments;
    const Color asphalt{64, 64, 66, 255};
    for (const Road& r : roads)
        for (size_t i = 0; i + 1 < r.pts.size(); ++i) {
            segments.push_back({r.pts[i], r.pts[i + 1]});
            Vector3 mid = Vector3Lerp(r.pts[i], r.pts[i + 1], 0.5f);
            AddRoad(builders(mid.x, mid.z).solid, t, r.pts[i], r.pts[i + 1], r.width, asphalt);
        }

    // ---- города: сетка улиц, дома вдоль улиц, фонари
    struct Town { Vector3 c; float radius; };
    const Town towns[] = {{{1600, 0, -1700}, 1100}, {{-6400, 0, 2300}, 550}, {{7000, 0, -5600}, 350}};
    const Color walls[] = {{226, 218, 200, 255}, {210, 196, 170, 255}, {236, 232, 224, 255}, {200, 170, 140, 255}, {180, 186, 192, 255}};
    const Color roofs[] = {{150, 60, 48, 255}, {110, 54, 44, 255}, {90, 90, 96, 255}, {130, 80, 50, 255}};
    std::vector<Vector3> houses;
    for (const Town& tw : towns) {
        const float block = 110.0f;
        for (float gx = -tw.radius; gx <= tw.radius; gx += block) {
            for (float gz = -tw.radius; gz <= tw.radius; gz += block) {
                float x = tw.c.x + gx, z = tw.c.z + gz;
                if (gx * gx + gz * gz > tw.radius * tw.radius) continue;
                // улицы по двум направлениям
                for (int dir = 0; dir < 2; ++dir) {
                    Vector3 a{x, 0, z}, b{x + (dir ? block : 0), 0, z + (dir ? 0 : block)};
                    if ((b.x - tw.c.x) * (b.x - tw.c.x) + (b.z - tw.c.z) * (b.z - tw.c.z) > tw.radius * tw.radius) continue;
                    if (NearRunway(a.x, a.z, 150) || t.IsWater(a.x, a.z)) continue;
                    AddRoad(builders(x, z).solid, t, a, b, 6, asphalt);
                    segments.push_back({a, b});
                    Vector3 lamp{a.x * 0.5f + b.x * 0.5f, 0, a.z * 0.5f + b.z * 0.5f};
                    lamp.y = t.SurfaceHeight(lamp.x, lamp.z) + 7.0f;
                    streetLights_.push_back(lamp);
                    // дома по обе стороны улицы
                    for (float s = 15; s < block - 10; s += 24) {
                        for (int side = -1; side <= 1; side += 2) {
                            if (Rand01() < 0.2f) continue;
                            float px = dir ? x + s : x + side * 16.0f;
                            float pz = dir ? z + side * 16.0f : z + s;
                            if (NearRunway(px, pz, 180) || t.IsWater(px, pz)) continue;
                            Vector3 n = t.Normal(px, pz);
                            if (n.y < 0.94f) continue;
                            float y = t.SurfaceHeight(px, pz);
                            bool big = Vector3Distance({px, 0, pz}, tw.c) < tw.radius * 0.35f && Rand01() < 0.4f;
                            float w = big ? RandRange(14, 22) : RandRange(8, 12);
                            float d = big ? RandRange(12, 18) : RandRange(7, 10);
                            float h = big ? RandRange(10, 24) : RandRange(4.5f, 7);
                            float yaw = dir ? 0.0f : PI * 0.5f;
                            AddHouse(builders(px, pz), {px, y, pz}, w, d, h, yaw, walls[(int)(Rand01() * 5) % 5],
                                     roofs[(int)(Rand01() * 4) % 4], Rand01() < 0.75f);
                            houses.push_back({px, y, pz});
                        }
                    }
                }
            }
        }
    }

    // ---- леса: там, где рельеф раскрашен под лес; не на полосах, дорогах и в городах
    auto nearRoad = [&](float x, float z) {
        for (auto& sg : segments) {
            Vector3 a = sg.first, b = sg.second;
            float abx = b.x - a.x, abz = b.z - a.z, l2 = abx * abx + abz * abz;
            float u = l2 > 0 ? Clampf(((x - a.x) * abx + (z - a.z) * abz) / l2, 0, 1) : 0;
            float dx = a.x + abx * u - x, dz = a.z + abz * u - z;
            if (dx * dx + dz * dz < 18.0f * 18.0f) return true;
        }
        return false;
    };
    const Color pine{40, 82, 46, 255}, pine2{56, 96, 50, 255}, birch{88, 120, 58, 255};
    int trees = 0;
    const float spacing = 42.0f;
    for (float x = -16000; x < 16000 && trees < 45000; x += spacing) {
        for (float z = -16000; z < 16000; z += spacing) {
            float px = x + RandRange(-15, 15), pz = z + RandRange(-15, 15);
            float f = t.ForestAmount(px, pz);
            if (f < 0.35f || Rand01() > f * 0.9f) continue;
            float h0 = t.GroundHeight(px, pz);
            if (h0 < 8.0f || h0 > 1150.0f) continue;
            if (t.Normal(px, pz).y < 0.8f) continue;
            if (NearRunway(px, pz, 250)) continue;
            bool inTown = false;
            for (const Town& tw : towns)
                if (Vector3Distance({px, 0, pz}, tw.c) < tw.radius + 60) inTown = true;
            if (inTown || nearRoad(px, pz)) continue;
            float th = RandRange(9, 18), tr = th * RandRange(0.22f, 0.3f);
            Color c = Rand01() < 0.5f ? pine : (Rand01() < 0.6f ? pine2 : birch);
            AddTree(builders(px, pz).solid, {px, h0 - 0.5f, pz}, th, tr, Shade(c, RandRange(0.85f, 1.15f)));
            ++trees;
        }
    }
    TraceLog(LOG_INFO, "SCENERY: %d trees, %d houses, %d street lights", trees, (int)houses.size(), (int)streetLights_.size());

    for (auto& kv : cells) {
        int cx = kv.first.first, cz = kv.first.second;
        Chunk ch;
        ch.center = {(cx + 0.5f) * CHUNK, 0, (cz + 0.5f) * CHUNK};
        kv.second.solid.Flush();
        kv.second.glow.Flush();
        ch.solid = kv.second.solid.out;
        ch.glow = kv.second.glow.out;
        chunks_.push_back(ch);
    }

    lit_ = LoadMaterialDefault();
    lit_.shader = litShader;
    unlit_ = LoadMaterialDefault();
    built_ = true;
}

void Scenery::Unload()
{
    if (!built_) return;
    for (Chunk& c : chunks_) {
        for (Mesh& m : c.solid) UnloadMesh(m);
        for (Mesh& m : c.glow) UnloadMesh(m);
    }
    chunks_.clear();
    MemFree(lit_.maps);
    UnloadMaterial(unlit_);
    built_ = false;
}

void Scenery::Draw(Vector3 camPos, float drawDist, bool lights) const
{
    if (!built_) return;
    rlDisableBackfaceCulling();   // стволы и окна — одиночные плоскости
    for (const Chunk& c : chunks_) {
        float dx = c.center.x - camPos.x, dz = c.center.z - camPos.z;
        if (sqrtf(dx * dx + dz * dz) > drawDist + CHUNK * 0.75f) continue;
        for (const Mesh& m : c.solid) DrawMesh(m, lit_, MatrixIdentity());
        if (lights)
            for (const Mesh& m : c.glow) DrawMesh(m, unlit_, MatrixIdentity());
    }
    rlEnableBackfaceCulling();
}
