#include "Effects.h"
#include "Aircraft.h"

#include "rlgl.h"

#include <algorithm>

using namespace fs;

namespace {

unsigned g_seed = 12345u;
float Rnd()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return ((g_seed >> 8) & 0xFFFF) / 65535.0f;
}
float Rnd(float a, float b) { return a + (b - a) * Rnd(); }

// Мягкое пятно для частиц и облаков.
Texture2D MakeSoftTexture()
{
    const int n = 64;
    Image img = GenImageColor(n, n, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
            float d = sqrtf(dx * dx + dy * dy);
            float a = Clampf(1.0f - d, 0, 1);
            a = a * a * (3 - 2 * a);
            px[y * n + x] = Color{255, 255, 255, (unsigned char)(a * 255)};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// Текстура сплошной облачности: светлые и тёмные «клочья».
Texture2D MakeCloudTexture()
{
    Image img = GenImagePerlinNoise(256, 256, 0, 0, 4.0f);
    Color* px = (Color*)img.data;
    for (int i = 0; i < 256 * 256; ++i) {
        float v = px[i].r / 255.0f;
        unsigned char c = (unsigned char)(200 + 55 * v);
        px[i] = Color{c, c, c, 255};
    }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// Большой круглый слой облаков: к краям прозрачнее, чтобы не было видно границы.
Mesh MakeLayerMesh()
{
    const int rings = 12, seg = 48;
    const float R = 60000.0f;
    Mesh m{};
    m.vertexCount = 1 + rings * seg;
    m.triangleCount = seg + (rings - 1) * seg * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
    m.indices = (unsigned short*)MemAlloc(m.triangleCount * 3 * sizeof(unsigned short));
    auto setV = [&](int i, float x, float z, float alpha) {
        m.vertices[i * 3 + 0] = x;
        m.vertices[i * 3 + 1] = 0;
        m.vertices[i * 3 + 2] = z;
        m.texcoords[i * 2 + 0] = x / 3000.0f;
        m.texcoords[i * 2 + 1] = z / 3000.0f;
        m.colors[i * 4 + 0] = m.colors[i * 4 + 1] = m.colors[i * 4 + 2] = 255;
        m.colors[i * 4 + 3] = (unsigned char)(alpha * 255);
    };
    setV(0, 0, 0, 1);
    for (int r = 0; r < rings; ++r) {
        float rr = R * powf((r + 1.0f) / rings, 1.6f);
        float alpha = 1.0f - SmoothStep(0.55f, 1.0f, (r + 1.0f) / rings);
        for (int s = 0; s < seg; ++s) {
            float a = 2 * PI * s / seg;
            setV(1 + r * seg + s, cosf(a) * rr, sinf(a) * rr, alpha);
        }
    }
    int k = 0;
    for (int s = 0; s < seg; ++s) {
        m.indices[k++] = 0;
        m.indices[k++] = (unsigned short)(1 + (s + 1) % seg);
        m.indices[k++] = (unsigned short)(1 + s);
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int s = 0; s < seg; ++s) {
            unsigned short a = (unsigned short)(1 + r * seg + s), b = (unsigned short)(1 + r * seg + (s + 1) % seg);
            unsigned short c = (unsigned short)(a + seg), d = (unsigned short)(b + seg);
            m.indices[k++] = a; m.indices[k++] = b; m.indices[k++] = c;
            m.indices[k++] = b; m.indices[k++] = d; m.indices[k++] = c;
        }
    UploadMesh(&m, false);
    return m;
}

Color Mul(Color c, Vector3 k, float a = 1.0f)
{
    return Color{(unsigned char)Clampf(c.r * k.x, 0, 255), (unsigned char)Clampf(c.g * k.y, 0, 255),
                 (unsigned char)Clampf(c.b * k.z, 0, 255), (unsigned char)Clampf(c.a * a, 0, 255)};
}

} // namespace

void Effects::Init()
{
    soft_ = MakeSoftTexture();
    cloudTex_ = MakeCloudTexture();
    layer_ = MakeLayerMesh();
    layerMat_ = LoadMaterialDefault();
    layerMat_.maps[MATERIAL_MAP_DIFFUSE].texture = cloudTex_;

    g_seed = 777u;
    for (int i = 0; i < 900; ++i) {
        float az = Rnd(0, 2 * PI), el = asinf(Rnd(0.05f, 1.0f));
        stars_.push_back({cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az)});
    }
    rain_.resize(900);
    for (Vector3& r : rain_) r = {Rnd(-40, 40), Rnd(-20, 30), Rnd(-40, 40)};
    drops_.resize(70);
    for (Drop& d : drops_) d = {Rnd(), Rnd(), Rnd(2, 6), Rnd(0.2f, 1.0f)};
}

void Effects::Unload()
{
    UnloadTexture(soft_);
    UnloadMesh(layer_);
    layerMat_.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    UnloadMaterial(layerMat_);
    UnloadTexture(cloudTex_);
}

void Effects::SetEnvironment(const Environment& env)
{
    // Кучевые облака: скопления «клубов» над островом.
    puffs_.clear();
    if (!env.clouds) return;
    g_seed = 4242u;
    for (int c = 0; c < 150; ++c) {
        Vector3 center{Rnd(-26000, 26000), env.cloudBase, Rnd(-26000, 26000)};
        float scale = Rnd(0.6f, 1.4f);
        int n = 7 + (int)Rnd(0, 8);
        for (int i = 0; i < n; ++i) {
            float y = Rnd(0, 1);
            Puff p;
            p.pos = {center.x + Rnd(-500, 500) * scale, center.y + y * 380 * scale, center.z + Rnd(-500, 500) * scale};
            p.size = Rnd(320, 620) * scale * (1.0f - 0.35f * y);
            p.shade = 0.72f + 0.28f * y;   // низ облака темнее
            puffs_.push_back(p);
        }
    }
}

void Effects::Clear() { particles_.clear(); }

void Effects::Spawn(Vector3 p, Vector3 v, float life, float size, float grow, Color c)
{
    if (particles_.size() > 1500) return;
    particles_.push_back({p, v, life, life, size, grow, c});
}

void Effects::Update(const Aircraft& a, const Environment& env, Vector3 wind, float dt, bool touchdown)
{
    const AircraftType& t = a.Type();
    // Дым из-под колёс в момент касания: резина разгоняется до скорости самолёта.
    if (touchdown && !a.crashed) {
        float strength = Clampf(Vector3Length(a.vel) / 60.0f, 0.3f, 1.0f);
        for (int k : {LEFT_MAIN, RIGHT_MAIN})
            for (int i = 0; i < 14; ++i) {
                Vector3 p = a.ToWorld(t.contacts[k]);
                Vector3 v = Vector3Add(Vector3Scale(a.vel, Rnd(0.2f, 0.6f)), {Rnd(-2, 2), Rnd(0.5f, 2.5f), Rnd(-2, 2)});
                Spawn(p, v, Rnd(1.5f, 3.5f), 1.5f * strength * (t.span / 15.9f), 3.0f, Color{225, 225, 225, 170});
            }
    }
    // Выхлоп реактивных двигателей (на большом газе) и дым горящего двигателя.
    exhaustTimer_ += dt;
    fireTimer_ += dt;
    if (!a.crashed) {
        for (int i = 0; i < t.engineCount; ++i) {
            const Engine& e = a.engines[i];
            Vector3 nozzleLocal = t.enginePos[i];
            nozzleLocal.x -= t.jet ? 2.2f * (t.span / 15.9f) : 0.5f;
            Vector3 nozzle = a.ToWorld(nozzleLocal);
            Vector3 back = Vector3Scale(a.Forward(), -1);
            if (t.jet && e.n1 > 70.0f && exhaustTimer_ > 0.05f) {
                float k = (e.n1 - 70.0f) / 30.0f;
                Spawn(nozzle, Vector3Add(Vector3Scale(back, 25.0f), wind), 0.8f, 0.8f * t.span / 15.9f, 3.0f,
                      Color{140, 140, 145, (unsigned char)(25 * k)});
            }
            if (e.fire && fireTimer_ > 0.03f) {
                Vector3 v = Vector3Add(Vector3Scale(a.vel, 0.6f), {Rnd(-1, 1), Rnd(1, 3), Rnd(-1, 1)});
                Spawn(nozzle, v, 0.6f, 1.2f, 2.0f, Color{255, (unsigned char)Rnd(90, 180), 30, 230});
                Spawn(nozzle, Vector3Add(v, {0, 2, 0}), Rnd(3, 6), 2.0f, 4.0f, Color{40, 38, 36, 150});
            }
        }
    }
    if (exhaustTimer_ > 0.05f) exhaustTimer_ = 0;
    if (fireTimer_ > 0.03f) fireTimer_ = 0;

    for (Particle& p : particles_) {
        p.life -= dt;
        p.vel = Vector3Add(Vector3Scale(Vector3Subtract(p.vel, wind), 1.0f - 1.5f * dt), wind);   // тормозится о воздух
        p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
        p.size += p.grow * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0; }),
                     particles_.end());

    // Капли на стекле стекают тем быстрее, чем выше скорость.
    if (env.rain) {
        float speed = Clampf(a.ias / 60.0f, 0.05f, 2.0f);
        for (Drop& d : drops_) {
            d.y -= d.speed * speed * dt * 0.6f;
            d.x += (d.x - 0.5f) * speed * dt * 0.3f;
            if (d.y < 0 || d.x < 0 || d.x > 1) d = {Rnd(), Rnd(0.6f, 1.0f), Rnd(2, 6), Rnd(0.2f, 1.0f)};
        }
    }
}

void Effects::DrawSkyObjects(const Camera3D& cam, const Environment& env) const
{
    if (!env.night || env.overcast) return;
    rlDisableDepthMask();
    for (const Vector3& s : stars_) {
        Vector3 p = Vector3Add(cam.position, Vector3Scale(s, 30000.0f));
        float b = 0.5f + 0.5f * fabsf(sinf(s.x * 91.0f + s.z * 37.0f));
        DrawBillboard(cam, soft_, p, 60.0f + 70.0f * b, Color{230, 235, 255, (unsigned char)(150 + 100 * b)});
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Effects::DrawClouds(const Camera3D& cam, const Environment& env) const
{
    Vector3 cp = cam.position;
    if (env.overcast) {
        // Нижняя кромка — серая, верх — освещённый солнцем.
        bool inside = cp.y > env.cloudBase && cp.y < env.cloudTop;
        if (!inside) {
            rlDisableBackfaceCulling();
            Vector3 under = env.night ? Vector3{0.10f, 0.11f, 0.14f} : Vector3Scale(Vector3Add(env.ambient, env.sunLight), 0.75f);
            Vector3 over = env.night ? Vector3{0.25f, 0.27f, 0.33f}
                                     : Vector3Scale(Vector3Add(env.ambient, Vector3Scale(env.sunLight, 4.0f)), 0.75f);
            Material m = layerMat_;
            if (cp.y < env.cloudBase) {
                m.maps[MATERIAL_MAP_DIFFUSE].color = Mul(WHITE, under);
                DrawMesh(layer_, m, MatrixTranslate(cp.x, env.cloudBase, cp.z));
            } else {
                m.maps[MATERIAL_MAP_DIFFUSE].color = Mul(WHITE, over);
                DrawMesh(layer_, m, MatrixTranslate(cp.x, env.cloudTop, cp.z));
            }
            rlEnableBackfaceCulling();
        }
    }
    if (puffs_.empty()) return;
    // Кучевые: billboard-клубы, отсортированные от дальних к ближним.
    std::vector<std::pair<float, int>> order;
    order.reserve(puffs_.size());
    for (int i = 0; i < (int)puffs_.size(); ++i) {
        float d = Vector3Distance(cp, puffs_[i].pos);
        if (d < 45000.0f) order.push_back({d, i});
    }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    Vector3 light = Vector3Add(env.ambient, Vector3Scale(env.sunLight, 1.1f));
    rlDisableDepthMask();
    for (auto& o : order) {
        const Puff& p = puffs_[o.second];
        float fade = 1.0f - SmoothStep(25000.0f, 45000.0f, o.first);
        float near = SmoothStep(p.size * 0.3f, p.size * 1.2f, o.first);   // изнутри облако не рисуем
        Color c = Mul(WHITE, Vector3Scale(light, p.shade), 0.85f * fade * near);
        DrawBillboard(cam, soft_, p.pos, p.size * 2.0f, c);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Effects::DrawParticles(const Camera3D& cam) const
{
    if (particles_.empty()) return;
    std::vector<std::pair<float, int>> order;
    order.reserve(particles_.size());
    for (int i = 0; i < (int)particles_.size(); ++i) order.push_back({Vector3Distance(cam.position, particles_[i].pos), i});
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    rlDisableDepthMask();
    for (auto& o : order) {
        const Particle& p = particles_[o.second];
        float k = Clampf(p.life / p.maxLife, 0, 1);
        Color c = p.color;
        c.a = (unsigned char)(c.a * k);
        DrawBillboard(cam, soft_, p.pos, p.size, c);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Effects::DrawRain3D(const Camera3D& cam, const Environment& env) const
{
    if (!env.rain || cam.position.y > env.cloudBase) return;
    double t = GetTime();
    Color c = env.night ? Color{120, 130, 150, 90} : Color{190, 200, 215, 110};
    for (const Vector3& r : rain_) {
        // Капли «живут» в кубе вокруг камеры и падают со скоростью ~9 м/с.
        float y = fmodf(r.y - (float)t * 9.0f, 50.0f);
        if (y < 0) y += 50.0f;
        Vector3 p{cam.position.x + r.x, cam.position.y + y - 25.0f, cam.position.z + r.z};
        DrawLine3D(p, {p.x + 0.05f, p.y - 1.2f, p.z}, c);
    }
}

void Effects::DrawWindshield(const Environment& env, float airspeed, bool belowClouds) const
{
    if (!env.rain || !belowClouds) return;
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    float streak = Clampf(airspeed / 40.0f, 0.2f, 3.0f);
    for (const Drop& d : drops_) {
        float x = d.x * sw, y = (1.0f - d.y) * sh * 0.66f;
        Color c{200, 215, 235, 70};
        DrawCircle((int)x, (int)y, d.r, c);
        DrawLineEx({x, y}, {x + (x - sw * 0.5f) * 0.02f * streak, y + d.r * 3.0f * streak}, d.r * 0.6f, Color{200, 215, 235, 40});
    }
}
