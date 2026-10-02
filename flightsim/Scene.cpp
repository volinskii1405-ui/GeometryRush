#include "Scene.h"
#include "Aircraft.h"
#include "Effects.h"
#include "Terrain.h"
#include "World.h"

#include "rlgl.h"

#include <algorithm>

using namespace fs;

namespace {

const char* kLitVS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
void main()
{
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Освещение: рассеянный свет неба + солнце/луна + посадочная фара самолёта; туман по дальности.
const char* kLitFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform vec3 sunLight;
uniform vec3 ambient;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float specular;
uniform vec3 llPos;
uniform vec3 llDir;
uniform float llOn;
out vec4 finalColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 base = fragColor.rgb * colDiffuse.rgb;
    float diff = max(dot(n, sunDir), 0.0);
    float sky = 0.56 + 0.44 * n.y;
    vec3 light = ambient * sky + sunLight * diff;
    if (llOn > 0.5) {
        vec3 L = fragPos - llPos;
        float d = length(L);
        vec3 Ld = L / max(d, 0.001);
        float spot = smoothstep(0.88, 0.985, dot(Ld, llDir));
        float att = 1.0 / (1.0 + d * d / (220.0 * 220.0));
        light += vec3(1.0, 0.95, 0.85) * spot * att * 2.4 * max(dot(n, -Ld), 0.25);
    }
    vec3 col = base * light;
    vec3 toEye = viewPos - fragPos;
    float dist = length(toEye);
    vec3 h = normalize(toEye / max(dist, 0.001) + sunDir);
    col += sunLight * 1.3 * specular * pow(max(dot(n, h), 0.0), 48.0);
    float f = 1.0 - exp(-pow(dist * fogDensity, 1.5));
    col = mix(col, fogColor, clamp(f, 0.0, 1.0));
    finalColor = vec4(col, fragColor.a * colDiffuse.a);
}
)";

// Цепочка преобразований: каждое следующее применяется после предыдущего.
struct Xf {
    Matrix m = MatrixIdentity();
    Xf& S(float x, float y, float z) { m = MatrixMultiply(m, MatrixScale(x, y, z)); return *this; }
    Xf& T(float x, float y, float z) { m = MatrixMultiply(m, MatrixTranslate(x, y, z)); return *this; }
    Xf& T(Vector3 v) { return T(v.x, v.y, v.z); }
    Xf& RX(float a) { m = MatrixMultiply(m, MatrixRotateX(a)); return *this; }
    Xf& RY(float a) { m = MatrixMultiply(m, MatrixRotateY(a)); return *this; }
    Xf& RZ(float a) { m = MatrixMultiply(m, MatrixRotateZ(a)); return *this; }
    Xf& Then(const Matrix& o) { m = MatrixMultiply(m, o); return *this; }
    operator Matrix() const { return m; }
};

Color Lerp(Color a, Color b, float t)
{
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

Mesh GenSkyDome(Color horizon, Color zenith)
{
    const int rings = 16, slices = 32;
    Mesh m{};
    m.vertexCount = (rings + 1) * (slices + 1);
    m.triangleCount = rings * slices * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
    m.indices = (unsigned short*)MemAlloc(m.triangleCount * 3 * sizeof(unsigned short));
    Color below{(unsigned char)(horizon.r * 0.85f), (unsigned char)(horizon.g * 0.88f), (unsigned char)(horizon.b * 0.9f), 255};
    for (int r = 0; r <= rings; ++r) {
        float el = ::Lerp(-25.0f, 90.0f, (float)r / rings) * DEG2RAD;
        float t = el > 0 ? powf(sinf(el), 0.55f) : 0.0f;
        Color c = el < 0 ? below : Lerp(horizon, zenith, t);
        for (int s = 0; s <= slices; ++s) {
            float az = 2 * PI * s / slices;
            int v = r * (slices + 1) + s;
            m.vertices[v * 3 + 0] = cosf(el) * cosf(az);
            m.vertices[v * 3 + 1] = sinf(el);
            m.vertices[v * 3 + 2] = cosf(el) * sinf(az);
            m.colors[v * 4 + 0] = c.r;
            m.colors[v * 4 + 1] = c.g;
            m.colors[v * 4 + 2] = c.b;
            m.colors[v * 4 + 3] = 255;
        }
    }
    int k = 0;
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < slices; ++s) {
            unsigned short a = (unsigned short)(r * (slices + 1) + s), b = (unsigned short)(a + 1);
            unsigned short c = (unsigned short)(a + slices + 1), d = (unsigned short)(c + 1);
            m.indices[k++] = a; m.indices[k++] = c; m.indices[k++] = b;
            m.indices[k++] = b; m.indices[k++] = c; m.indices[k++] = d;
        }
    UploadMesh(&m, false);
    return m;
}

Texture2D MakeGlowTexture()
{
    const int n = 32;
    Image img = GenImageColor(n, n, BLANK);
    Color* px = (Color*)img.data;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
            float d = sqrtf(dx * dx + dy * dy);
            float a = Clampf(1.0f - d, 0, 1);
            px[y * n + x] = Color{255, 255, 255, (unsigned char)(255 * a * a)};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// Цифры разметки ВПП из «сегментов», как на семисегментном индикаторе.
const unsigned char kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

} // namespace

const char* CamModeName(CamMode m)
{
    switch (m) {
    case CamMode::Cockpit: return ui::L("COCKPIT", "КАБИНА");
    case CamMode::Chase: return ui::L("CHASE", "СЗАДИ");
    case CamMode::Orbit: return ui::L("EXTERNAL", "СНАРУЖИ");
    case CamMode::Tower: return ui::L("TOWER", "С ВЫШКИ");
    default: return "";
    }
}

void Scene::Init()
{
    lit_ = LoadShaderFromMemory(kLitVS, kLitFS);
    lit_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lit_, "matModel");
    lit_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lit_, "matNormal");
    locSun_ = GetShaderLocation(lit_, "sunDir");
    locSunLight_ = GetShaderLocation(lit_, "sunLight");
    locAmbient_ = GetShaderLocation(lit_, "ambient");
    locView_ = GetShaderLocation(lit_, "viewPos");
    locFog_ = GetShaderLocation(lit_, "fogColor");
    locFogDensity_ = GetShaderLocation(lit_, "fogDensity");
    locSpec_ = GetShaderLocation(lit_, "specular");
    locLlPos_ = GetShaderLocation(lit_, "llPos");
    locLlDir_ = GetShaderLocation(lit_, "llDir");
    locLlOn_ = GetShaderLocation(lit_, "llOn");
    SetSpecular(0.0f);

    mat_ = LoadMaterialDefault();
    mat_.shader = lit_;
    unlit_ = LoadMaterialDefault();

    sphere_ = GenMeshSphere(1.0f, 14, 22);
    cube_ = GenMeshCube(1.0f, 1.0f, 1.0f);
    cylinder_ = GenMeshCylinder(1.0f, 1.0f, 16);
    cone_ = GenMeshCone(1.0f, 1.0f, 12);
    soft_ = MakeGlowTexture();
    SetEnvironment(Environment::Make(TimeOfDay::Day, WeatherKind::Clear));

    camera.up = {0, 1, 0};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}

void Scene::BuildWorld(const Terrain& t) { scenery_.Build(t, lit_); }

void Scene::SetEnvironment(const Environment& env)
{
    env_ = env;
    if (sky_.vertexCount) UnloadMesh(sky_);
    sky_ = GenSkyDome(env.horizon, env.zenith);
    SetShaderValue(lit_, locSun_, &env.sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit_, locSunLight_, &env.sunLight, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit_, locAmbient_, &env.ambient, SHADER_UNIFORM_VEC3);
}

void Scene::Unload()
{
    scenery_.Unload();
    UnloadMesh(sphere_);
    UnloadMesh(cube_);
    UnloadMesh(cylinder_);
    UnloadMesh(cone_);
    UnloadMesh(sky_);
    UnloadTexture(soft_);
    MemFree(mat_.maps);   // шейдер выгружаем отдельно
    UnloadMaterial(unlit_);
    UnloadShader(lit_);
}

void Scene::SetSpecular(float s)
{
    if (s == spec_) return;
    spec_ = s;
    SetShaderValue(lit_, locSpec_, &s, SHADER_UNIFORM_FLOAT);
}

void Scene::Part(const Mesh& mesh, Matrix local, Matrix world, Color c, float spec)
{
    SetSpecular(spec);
    mat_.maps[MATERIAL_MAP_DIFFUSE].color = c;
    DrawMesh(mesh, mat_, MatrixMultiply(local, world));
}

// Цилиндр (стойка, подкос) между двумя точками в связанных осях.
void Scene::PartBetween(Vector3 a, Vector3 b, float r, const Matrix& world, Color c)
{
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-3f) return;
    Matrix orient = QuaternionToMatrix(QuaternionFromVector3ToVector3({0, 1, 0}, Vector3Scale(d, 1.0f / len)));
    Part(cylinder_, Xf().S(r, len, r).Then(orient).T(a), world, c, 0.5f);
}

void Scene::Box(Vector3 center, Vector3 size, Color c, float spec)
{
    Part(cube_, Xf().S(size.x, size.y, size.z).T(center.x, center.y, center.z), MatrixIdentity(), c, spec);
}

void Scene::Glow(Vector3 pos, Color c, float size, float farScale) { glows_.push_back({pos, c, size, farScale}); }

void Scene::DrawGlows()
{
    // Огни видны издалека: размер ореола растёт с расстоянием, чтобы точка не исчезала.
    float strength = env_.night ? 1.0f : (env_.dusk ? 0.7f : 0.35f);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (const GlowPt& g : glows_) {
        float d = Vector3Distance(camera.position, g.p);
        float size = fmaxf(g.size, d * g.farScale * (camera.fovy / 60.0f));
        Color c = g.c;
        c.a = (unsigned char)(c.a * strength * (g.farScale < 0.003f ? 1.0f / (1.0f + d / 2500.0f) : 1.0f));
        DrawBillboard(camera, soft_, g.p, size, c);
    }
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    glows_.clear();
}

// ------------------------------------------------------------------ камеры

void Scene::ResetCamera(const Aircraft& a)
{
    float k = a.Type().span / 15.9f;
    chaseOffset_ = Vector3Add(Vector3Scale(a.Forward(), -28.0f * k), {0, 7.0f * k, 0});
    orbitDist_ = 35.0f * k;
    lookYaw_ = lookPitch_ = 0;
}

void Scene::MenuCamera(Vector3 target, float time)
{
    float a = time * 0.05f;
    camera.position = {target.x + cosf(a) * 120.0f, target.y + 28.0f, target.z + sinf(a) * 120.0f};
    camera.target = target;
    camera.up = {0, 1, 0};
    camera.fovy = 50.0f;
}

// Тряска: предупреждение о сваливании («бафтинг»), превышение скорости и M, выпущенные интерцепторы
// и шасси на большой скорости, неровности полосы (на траве — сильнее), разрушение конструкции.
float Scene::ShakeAmount(const Aircraft& a) const
{
    if (a.crashed) return 0.0f;
    const AircraftType& t = a.Type();
    float iasKt = a.ias * MS_TO_KT;
    float airborne = a.onGround ? 0.3f : 1.0f;
    float s = 0.0f;
    if (a.ias > 15.0f) s += 0.55f * SmoothStep(a.stallAlpha - 4.0f * DEG2RAD, a.stallAlpha + 2.0f * DEG2RAD, a.alpha) * airborne;
    s += 0.6f * SmoothStep(t.vmo, t.vmo + 40.0f, iasKt);
    s += 0.5f * SmoothStep(0.80f, 0.88f, a.mach);
    s += 0.12f * a.speedbrake * SmoothStep(120.0f, 300.0f, iasKt);
    if (t.retractableGear) s += 0.08f * a.gear * SmoothStep(150.0f, 260.0f, iasKt) * airborne;
    if (a.onGround) {
        float gs = sqrtf(a.vel.x * a.vel.x + a.vel.z * a.vel.z);
        int ap = world::NearestAirport(a.pos.x, a.pos.z);
        bool paved = world::GetAirport(ap).rwy.Contains(a.pos.x, a.pos.z, 40.0f);
        s += (paved ? 0.10f : 0.45f) * SmoothStep(2.0f, 50.0f, gs);
        if (a.tireFlat[0] || a.tireFlat[1]) s += 0.35f * SmoothStep(1.0f, 20.0f, gs);
    }
    if (a.Broken()) s += 0.5f;
    return Clampf(s, 0.0f, 1.2f);
}

void Scene::ApplyShake(const Aircraft& a, CamMode mode, float dt)
{
    shakeSmooth_ = ::Lerp(shakeSmooth_, ShakeAmount(a), fminf(dt * 6.0f, 1.0f));
    kick_ = fmaxf(kick_ - dt * 2.5f, 0.0f);
    shakeTime_ += dt;
    float k = a.Type().span / 15.9f;
    float amp = (0.035f * shakeSmooth_ + 0.12f * kick_ * kick_);
    if (mode == CamMode::Chase) amp *= 4.0f * k;
    else if (mode != CamMode::Cockpit) return;   // снаружи и с вышки камера не трясётся
    float tt = shakeTime_;
    // Сумма несоизмеримых синусов — «шум» без периодичности; толчок — затухающее колебание вниз-вверх.
    Vector3 off{sinf(tt * 37.0f) * 0.6f + sinf(tt * 59.0f + 1.3f) * 0.4f,
                sinf(tt * 43.0f + 0.7f) * 0.7f + sinf(tt * 71.0f + 2.1f) * 0.3f,
                sinf(tt * 31.0f + 2.9f) * 0.5f + sinf(tt * 53.0f + 0.4f) * 0.3f};
    off = Vector3Scale(off, amp);
    off.y -= 0.25f * kick_ * kick_ * sinf(tt * 22.0f) * (mode == CamMode::Chase ? 4.0f * k : 1.0f);
    Vector3 w = Rotate(off, a.rot);
    camera.position = Vector3Add(camera.position, w);
    camera.target = Vector3Add(camera.target, Vector3Scale(w, 0.6f));
}

void Scene::UpdateCamera(const Aircraft& a, CamMode mode, float dt)
{
    UpdateCameraBase(a, mode, dt);
    ApplyShake(a, mode, dt);
}

void Scene::UpdateCameraBase(const Aircraft& a, CamMode mode, float dt)
{
    const AircraftType& t = a.Type();
    const float k = t.span / 15.9f;
    // Мышь: в кабине ПКМ — оглядеться, снаружи ЛКМ/ПКМ — вращать, колесо — дистанция.
    Vector2 md = mouseLocked ? Vector2{} : GetMouseDelta();
    bool dragging = !mouseLocked && (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (mode != CamMode::Cockpit && IsMouseButtonDown(MOUSE_BUTTON_LEFT)));
    float wheel = mouseLocked ? 0.0f : GetMouseWheelMove();

    if (a.crashed && (mode == CamMode::Cockpit || mode == CamMode::Chase)) {
        // После катастрофы — вид со стороны, чтобы камера не оказалась внутри огня.
        Vector3 desired{-70.0f * k, 35.0f * k, 40.0f * k};
        chaseOffset_ = Vector3Lerp(chaseOffset_, desired, fminf(dt * 1.5f, 1.0f));
        camera.position = Vector3Add(a.pos, chaseOffset_);
        camera.target = a.pos;
        camera.up = {0, 1, 0};
        camera.fovy = 60.0f;
        return;
    }

    switch (mode) {
    case CamMode::Cockpit: {
        if (!mouseLocked && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            lookYaw_ = Clampf(lookYaw_ - md.x * 0.25f, -150.0f, 150.0f);
            lookPitch_ = Clampf(lookPitch_ - md.y * 0.25f, -60.0f, 70.0f);
        } else {
            lookYaw_ = ::Lerp(lookYaw_, 0.0f, fminf(dt * 4.0f, 1.0f));
            lookPitch_ = ::Lerp(lookPitch_, 0.0f, fminf(dt * 4.0f, 1.0f));
        }
        Vector3 eye = a.ToWorld(t.eye);
        Vector3 dirLocal{cosf(lookPitch_ * DEG2RAD) * cosf(lookYaw_ * DEG2RAD), sinf(lookPitch_ * DEG2RAD),
                         -cosf(lookPitch_ * DEG2RAD) * sinf(lookYaw_ * DEG2RAD)};
        camera.position = eye;
        camera.target = Vector3Add(eye, Rotate(dirLocal, a.rot));
        camera.up = a.Up();
        camera.fovy = 68.0f;
        break;
    }
    case CamMode::Chase: {
        Vector3 desired = Vector3Add(Vector3Scale(a.Forward(), -28.0f * k), Vector3Scale(a.Up(), 6.0f * k));
        desired.y += 2.0f * k;
        chaseOffset_ = Vector3Lerp(chaseOffset_, desired, fminf(dt * 3.0f, 1.0f));
        camera.position = Vector3Add(a.pos, chaseOffset_);
        camera.target = Vector3Add(a.pos, Vector3Scale(a.Forward(), 12.0f * k));
        camera.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, a.Up(), 0.3f));
        camera.fovy = 60.0f;
        break;
    }
    case CamMode::Orbit: {
        if (dragging) {
            orbitYaw_ += md.x * 0.3f;
            orbitPitch_ = Clampf(orbitPitch_ + md.y * 0.3f, -80.0f, 85.0f);
        }
        orbitDist_ = Clampf(orbitDist_ * (1.0f - wheel * 0.1f), 8.0f * k, 600.0f);
        float yaw = orbitYaw_ * DEG2RAD, pitch = orbitPitch_ * DEG2RAD;
        Vector3 off{cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        camera.position = Vector3Add(a.pos, Vector3Scale(off, orbitDist_));
        camera.target = a.pos;
        camera.up = {0, 1, 0};
        camera.fovy = 55.0f;
        break;
    }
    case CamMode::Tower: {
        int ap = world::NearestAirport(a.pos.x, a.pos.z);
        const Airport& port = world::GetAirport(ap);
        if (port.main) {
            camera.position = {250.0f, port.rwy.center.y + 33.0f, -330.0f};
        } else {
            Vector3 d = port.rwy.Dir(0);
            camera.position = Vector3Add(port.rwy.center, {-d.z * 110.0f, 14.0f, d.x * 110.0f});
        }
        camera.target = a.pos;
        camera.up = {0, 1, 0};
        float dist = Vector3Distance(camera.position, a.pos);
        camera.fovy = Clampf(2.0f * atanf(25.0f * k / fmaxf(dist, 1.0f)) * RAD2DEG, 1.5f, 70.0f);
        break;
    }
    default: break;
    }
}

// ------------------------------------------------------------------ отрисовка

void Scene::DrawSky()
{
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawMesh(sky_, unlit_, Xf().S(40000, 40000, 40000).T(camera.position.x, camera.position.y, camera.position.z));
    bool aboveClouds = env_.overcast && camera.position.y > env_.cloudTop;
    if (env_.showSun || aboveClouds) {
        Vector3 sun = Vector3Add(camera.position, Vector3Scale(env_.sunDir, 38000.0f));
        Color c = env_.night ? Color{220, 225, 235, 255} : (env_.time == TimeOfDay::Sunset ? Color{255, 190, 120, 255} : Color{255, 250, 225, 255});
        DrawSphere(sun, env_.night ? 900.0f : 1600.0f, c);
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void Scene::DrawAirport(int index, float time, Vector3 wind)
{
    const Airport& ap = world::GetAirport(index);
    const Runway& rw = ap.rwy;
    const float E = rw.center.y;
    const float L = rw.halfLen, W = rw.halfWidth;
    // Система координат полосы: x — по курсу торца [0], z — вправо.
    const Matrix M = MatrixMultiply(MatrixRotateY((90.0f - rw.heading) * DEG2RAD), MatrixTranslate(rw.center.x, 0, rw.center.z));
    auto P = [&](float x, float y, float z) { return Vector3Transform({x, y, z}, M); };
    auto LBox = [&](float x, float y, float z, float sx, float sy, float sz, Color c, float spec = 0.0f) {
        Part(cube_, Xf().S(sx, sy, sz).T(x, y, z), M, c, spec);
    };
    const Color asphalt{58, 60, 64, 255}, paint{232, 232, 228, 255};
    const float scale = W / 22.5f;   // разметка узкой полосы мельче

    LBox(0, E + 0.15f, 0, 2 * L, 0.3f, 2 * W, asphalt, 0.05f);
    LBox(0, E + 0.09f, 0, 2 * L + 60, 0.18f, 2 * W + 14 * scale, Color{92, 96, 92, 255});
    const float yP = E + 0.33f;
    for (float x = -L + 200 * scale; x < L - 200 * scale; x += 60.0f) LBox(x, yP, 0, 30, 0.06f, 0.9f, paint);
    for (float s : {-1.0f, 1.0f}) {
        LBox(0, yP, s * (W - 1.2f), 2 * L - 20, 0.06f, 0.9f, paint);
        for (int i = 0; i < 4; ++i) {   // «зебра» порога
            float z = (3.0f + i * 4.6f) * scale;
            LBox(s * (L - 24), yP, z, 30, 0.06f, 1.8f * scale, paint);
            LBox(s * (L - 24), yP, -z, 30, 0.06f, 1.8f * scale, paint);
        }
        float aim = L > 1000 ? 400.0f : 200.0f;
        LBox(s * (L - aim), yP, 8.0f * scale, 50 * scale, 0.06f, 7 * scale, paint);
        LBox(s * (L - aim), yP, -8.0f * scale, 50 * scale, 0.06f, 7 * scale, paint);
    }
    // Номера торцов из сегментов.
    auto digit = [&](int d, float ox, float oz, float upSign) {
        const float h = 18.0f * scale, w = 8.0f * scale, t = 1.6f * scale;
        struct Seg { float u, v; bool horiz; };
        const Seg segs[7] = {{h, 0, true}, {h * 0.75f, w * 0.5f, false}, {h * 0.25f, w * 0.5f, false}, {0, 0, true},
                             {h * 0.25f, -w * 0.5f, false}, {h * 0.75f, -w * 0.5f, false}, {h * 0.5f, 0, true}};
        for (int i = 0; i < 7; ++i) {
            if (!(kSegments[d] & (1 << i))) continue;
            float x = ox + upSign * segs[i].u, z = oz + upSign * segs[i].v;
            if (segs[i].horiz) LBox(x, yP, z, t, 0.06f, w + t, paint);
            else LBox(x, yP, z, h * 0.5f + t, 0.06f, t, paint);
        }
    };
    for (int end = 0; end < 2; ++end) {
        float s = end ? 1.0f : -1.0f;              // торец [0] — у -x, читается при посадке по +x
        float up = -s;
        const char* id = rw.ident[end];
        float base = s * (L - 60 * scale);
        digit(id[0] - '0', base, -6.5f * scale * up, up);
        digit(id[1] - '0', base, 6.5f * scale * up, up);
    }

    // Огни: боковые, торцевые, огни приближения (у полос с ILS), PAPI.
    const bool lightsBright = env_.dusk;
    for (float x = -L; x <= L + 1; x += 60.0f)
        for (float s : {-1.0f, 1.0f}) {
            Vector3 p = P(x, E + 0.5f, s * (W + 1.5f));
            DrawCube(p, 0.6f, 0.6f, 0.6f, Color{255, 245, 200, 255});
            if (lightsBright) Glow(p, Color{255, 236, 190, 220}, 2.5f);
        }
    for (float z = -W; z <= W; z += 4.5f)
        for (float s : {-1.0f, 1.0f}) {
            Vector3 p = P(s * (L + 2), E + 0.5f, z);
            DrawCube(p, 0.7f, 0.7f, 0.7f, s < 0 ? Color{60, 255, 90, 255} : Color{255, 60, 60, 255});
            if (lightsBright) Glow(p, s < 0 ? Color{80, 255, 110, 220} : Color{255, 80, 70, 200}, 2.5f);
        }
    for (int end = 0; end < 2; ++end) {
        float s = end ? 1.0f : -1.0f;   // торец порога: [0] у -x
        if (rw.ils) {
            for (float d = 30.0f; d <= 720.0f; d += 30.0f) {
                bool flash = fmodf(time * 2.0f - d / 720.0f, 1.0f) < 0.08f;   // «бегущий огонь»
                Color c = flash ? WHITE : Color{255, 240, 190, 255};
                float size = flash ? 2.2f : 1.0f;
                for (float z = -3; z <= 3; z += 3) {
                    Vector3 p = P(s * (L + d), E + 1.0f, z);
                    DrawCube(p, size, size, size, c);
                    if (lightsBright || flash) Glow(p, flash ? Color{255, 255, 255, 255} : Color{255, 230, 170, 200}, flash ? 8.0f : 3.0f);
                }
                if (fabsf(d - 300.0f) < 1.0f)
                    for (float z = -15; z <= 15; z += 3) {
                        Vector3 p = P(s * (L + d), E + 1.0f, z);
                        DrawCube(p, 1.0f, 1.0f, 1.0f, c);
                        if (lightsBright) Glow(p, Color{255, 230, 170, 200}, 3.0f);
                    }
            }
        }
        // PAPI слева от полосы по направлению посадки: белый — выше своего угла, красный — ниже.
        const float thresholds[4] = {3.5f, 3.17f, 2.83f, 2.5f};
        const float tdz = L > 1000 ? 300.0f : 150.0f;
        for (int i = 0; i < 4; ++i) {
            Vector3 lp = P(s * (L - tdz), E + 1.2f, s * (W + 10.0f + i * 9.0f));
            Vector3 d = Vector3Subtract(camera.position, lp);
            Vector3 land = rw.Dir(end);
            bool facing = d.x * land.x + d.z * land.z < 0;
            float ang = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) * RAD2DEG;
            Color c = !facing ? Color{60, 60, 60, 255} : (ang > thresholds[i] ? Color{255, 255, 255, 255} : Color{255, 30, 30, 255});
            DrawCube(lp, 3.0f, 1.6f, 1.2f, c);
            if (facing) Glow(lp, Color{c.r, c.g, c.b, 255}, 5.0f);
        }
    }

    // Постройки.
    if (ap.main) {
        Box({0, E + 0.12f, -95}, {20, 0.24f, 150}, Color{70, 72, 76, 255});
        Box({0, E + 0.27f, -95}, {0.5f, 0.06f, 150}, Color{220, 180, 40, 255});
        Box({0, E + 0.12f, -215}, {600, 0.24f, 100}, Color{70, 72, 76, 255});
        Box({-200, E + 9, -305}, {70, 18, 50}, Color{150, 158, 168, 255}, 0.2f);
        Box({-120, E + 7, -305}, {60, 14, 50}, Color{176, 172, 160, 255}, 0.2f);
        Box({80, E + 6, -300}, {110, 12, 40}, Color{205, 205, 200, 255}, 0.2f);
        Part(cylinder_, Xf().S(4, 28, 4).T(250, E, -330), MatrixIdentity(), Color{200, 200, 196, 255});
        Part(cylinder_, Xf().S(6.5f, 5, 6.5f).T(250, E + 28, -330), MatrixIdentity(), Color{40, 70, 90, 255}, 0.8f);
        Part(cylinder_, Xf().S(7, 0.8f, 7).T(250, E + 33, -330), MatrixIdentity(), Color{180, 180, 176, 255});
        if (env_.dusk) {
            Glow({250, E + 30.5f, -330}, Color{150, 220, 255, 160}, 9.0f);
            Glow({250, E + 34.5f, -330}, (fmodf(time, 1.5f) < 0.75f) ? Color{80, 255, 120, 255} : Color{255, 255, 255, 255}, 6.0f);
            for (float x = -250; x <= 250; x += 125) Glow({x, E + 12, -170}, Color{255, 200, 120, 200}, 10.0f);
        }
    } else {
        LBox(0, E + 5, W + 70, 30, 10, 22, Color{150, 140, 120, 255}, 0.2f);
        LBox(0, E + 0.1f, W + 40, 60, 0.2f, 40, Color{70, 72, 76, 255});
    }

    // Ветроуказатель: «колдун» вытягивается по ветру.
    {
        Vector3 base = P(-L + 150 * scale, E, -(W + 40));
        Part(cylinder_, Xf().S(0.12f, 7, 0.12f).T(base), MatrixIdentity(), Color{220, 220, 220, 255});
        float ws = Vector3Length(wind);
        float lift = SmoothStep(0.0f, 12.0f, ws);
        Vector3 flat = ws > 0.1f ? Vector3Normalize({wind.x, 0, wind.z}) : Vector3{1, 0, 0};
        Vector3 dir = Vector3Normalize(Vector3Add(Vector3Scale(flat, 0.15f + lift), {0, -(1.0f - lift) - 0.08f, 0}));
        Matrix orient = QuaternionToMatrix(QuaternionFromVector3ToVector3({0, -1, 0}, dir));
        float flap = 0.05f * sinf(time * 7.0f) * lift;
        Part(cone_, Xf().S(0.55f, 4.0f, 0.55f).RX(PI).RZ(flap).Then(orient).T(base.x, base.y + 7, base.z), MatrixIdentity(),
             Color{240, 110, 30, 255});
    }
}

// ---------------------------------------------------------------- модели самолётов

// Лёд на передней кромке и плоскостях: крыло белеет.
static Color Iced(Color c, float ice)
{
    float k = Clampf(ice * 0.8f, 0.0f, 0.8f);
    return Color{(unsigned char)::Lerp(c.r, 236, k), (unsigned char)::Lerp(c.g, 242, k), (unsigned char)::Lerp(c.b, 250, k), c.a};
}

// Какие части модели рисовать: целый самолёт без оторванных частей или только обломок.
enum { PM_BODY = 1, PM_WING_L = 2, PM_WING_R = 4, PM_TAIL = 8, PM_ALL = 15 };
static bool WingOn(int mask, float s) { return mask & (s < 0 ? PM_WING_L : PM_WING_R); }

void Scene::DrawLightJet(const Aircraft& a, const Matrix& world, int mask)
{
    const Color body{236, 238, 242, 255}, accent{196, 34, 46, 255}, glass{28, 38, 54, 255};
    const Color wing = Iced({208, 212, 218, 255}, a.ice), surface{178, 184, 194, 255}, dark{40, 42, 46, 255}, tire{25, 25, 25, 255};
    const bool B = mask & PM_BODY;

    if (B) {
    Part(sphere_, Xf().S(7.6f, 0.95f, 0.95f), world, body, 0.5f);
    Part(sphere_, Xf().S(7.63f, 0.16f, 0.968f).T(0, -0.1f, 0), world, accent, 0.5f);
    Part(sphere_, Xf().S(3.2f, 0.7f, 0.6f).T(-5.2f, 0.35f, 0), world, body, 0.5f);
    Part(sphere_, Xf().S(1.7f, 0.6f, 0.8f).T(4.9f, 0.42f, 0), world, glass, 0.9f);
    for (int i = 0; i < 5; ++i)
        for (float s : {-1.0f, 1.0f}) Part(cube_, Xf().S(0.38f, 0.3f, 0.05f).T(-2.2f + i * 1.1f, 0.25f, s * 0.915f), world, glass, 0.9f);
    }

    const Vector3 wingRoot{0.2f, -0.55f, 0.0f};
    for (float s : {-1.0f, 1.0f}) {
        if (WingOn(mask, s)) {
        Matrix wingFrame = Xf().RX(-s * 3.0f * DEG2RAD).RY(-s * 12.0f * DEG2RAD).T(wingRoot);
        Part(cube_, Xf().S(2.3f, 0.22f, 7.9f).T(0, 0, s * 3.95f).Then(wingFrame), world, wing, 0.4f);
        float ail = s > 0 ? -a.aileron : a.aileron;
        Part(cube_, Xf().S(0.55f, 0.12f, 2.6f).T(-0.275f, 0, 0).RZ(ail).T(-1.15f, 0, s * 6.3f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(0.6f, 0.14f, 3.7f).T(-0.3f, 0, 0).RZ(a.flaps * DEG2RAD).T(-1.15f, 0, s * 2.95f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(0.7f, 0.06f, 2.4f).T(-0.35f, 0, 0).RZ(-a.speedbrake * 45.0f * DEG2RAD).T(-0.25f, 0.12f, s * 3.2f).Then(wingFrame),
             world, surface, 0.3f);
        }
        if (B) {
        Part(sphere_, Xf().S(2.0f, 0.6f, 0.6f).T(-3.6f, 0.65f, s * 1.65f), world, Color{214, 218, 224, 255}, 0.6f);
        Part(sphere_, Xf().S(0.12f, 0.47f, 0.47f).T(-1.66f, 0.65f, s * 1.65f), world, dark, 0.2f);
        Part(cube_, Xf().S(1.2f, 0.18f, 0.75f).T(-3.6f, 0.58f, s * 1.0f), world, body, 0.4f);
        if (a.reverser > 0.05f)   // створки реверса
            Part(cube_, Xf().S(0.5f, 0.08f, 0.9f).RZ(-a.reverser * 0.8f).T(-5.3f, 0.65f + 0.45f, s * 1.65f), world, dark, 0.3f);
        }
        if (!(mask & PM_TAIL)) continue;
        Matrix stabFrame = Xf().RY(-s * 10.0f * DEG2RAD).T(-7.3f, 3.42f, 0.0f);
        Part(cube_, Xf().S(1.7f, 0.16f, 3.1f).T(0, 0, s * 1.55f).Then(stabFrame), world, wing, 0.4f);
        Part(cube_, Xf().S(0.6f, 0.1f, 2.9f).T(-0.3f, 0, 0).RZ(-a.elevator).T(-0.85f, 0, s * 1.6f).Then(stabFrame), world, surface, 0.3f);
    }
    if (!B) return;
    Matrix finFrame = Xf().RZ(25.0f * DEG2RAD).T(-6.2f, 0.7f, 0.0f);
    Part(cube_, Xf().S(2.4f, 3.0f, 0.24f).T(0, 1.5f, 0).Then(finFrame), world, body, 0.4f);
    Part(cube_, Xf().S(0.75f, 2.7f, 0.18f).T(-0.375f, 1.4f, 0).RY(a.rudder).T(-1.2f, 0, 0).Then(finFrame), world, accent, 0.3f);

    if (a.gear > 0.03f) {
        struct Leg { float x, z, top; bool nose; };
        const Leg legs[3] = {{4.9f, 0.0f, -0.6f, true}, {-0.6f, -1.8f, -0.62f, false}, {-0.6f, 1.8f, -0.62f, false}};
        for (const Leg& l : legs) {
            float wy = ::Lerp(l.top - 0.1f, -1.55f, a.gear);
            Part(cylinder_, Xf().S(0.07f, l.top - wy, 0.07f).T(l.x, wy, l.z), world, Color{150, 152, 158, 255}, 0.6f);
            float steer = l.nose ? -a.noseSteer : 0.0f;
            float r = l.nose ? 0.3f : 0.36f;
            Part(cylinder_, Xf().S(r, 0.24f, r).T(0, -0.12f, 0).RX(PI / 2).RY(steer).T(l.x, -1.9f + r, l.z), world, tire, 0.1f);
        }
    }
}

void Scene::DrawProp(const Aircraft& a, const Matrix& world, float time, int mask)
{
    const Color body{240, 240, 236, 255}, accent{30, 70, 150, 255}, glass{40, 52, 68, 255};
    const Color surface{214, 216, 220, 255}, dark{40, 40, 42, 255}, tire{25, 25, 25, 255};
    const bool B = mask & PM_BODY;

    if (B) {
    Part(sphere_, Xf().S(3.0f, 0.72f, 0.6f).T(-0.3f, 0.05f, 0), world, body, 0.4f);
    Part(sphere_, Xf().S(3.02f, 0.12f, 0.605f).T(-0.3f, -0.15f, 0), world, accent, 0.4f);
    Part(sphere_, Xf().S(2.7f, 0.32f, 0.28f).T(-3.3f, 0.25f, 0), world, body, 0.4f);
    Part(sphere_, Xf().S(1.25f, 0.42f, 0.62f).T(0.55f, 0.42f, 0), world, glass, 0.9f);
    Part(sphere_, Xf().S(0.95f, 0.55f, 0.55f).T(1.75f, 0.0f, 0), world, body, 0.5f);
    }
    // Крыло-высокоплан с подкосами
    for (float s : {-1.0f, 1.0f}) {
        if (WingOn(mask, s)) {
        Matrix wingFrame = Xf().RX(-s * 1.5f * DEG2RAD).T(-0.3f, 0.95f, 0.0f);
        Part(cube_, Xf().S(1.5f, 0.15f, 5.5f).T(0, 0, s * 2.75f).Then(wingFrame), world, Iced(body, a.ice), 0.4f);
        float ail = s > 0 ? -a.aileron : a.aileron;
        Part(cube_, Xf().S(0.4f, 0.08f, 1.8f).T(-0.2f, 0, 0).RZ(ail).T(-0.75f, 0, s * 4.3f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(0.45f, 0.08f, 2.4f).T(-0.22f, 0, 0).RZ(a.flaps * DEG2RAD).T(-0.75f, 0, s * 2.0f).Then(wingFrame), world, surface, 0.3f);
        PartBetween({-0.1f, -0.45f, s * 0.55f}, {-0.2f, 0.9f, s * 2.7f}, 0.04f, world, surface);
        }
        // Хвостовое оперение
        if (mask & PM_TAIL) {
        Part(cube_, Xf().S(0.9f, 0.08f, 1.7f).T(0, 0, s * 0.85f).T(-4.85f, 0.3f, 0), world, body, 0.4f);
        Part(cube_, Xf().S(0.4f, 0.06f, 1.6f).T(-0.2f, 0, 0).RZ(-a.elevator).T(-5.3f, 0.3f, s * 0.85f), world, surface, 0.3f);
        }
        if (!B) continue;
        // Шасси (неубирающееся) с обтекателями колёс
        PartBetween({-0.25f, -0.55f, s * 0.4f}, {-0.3f, -0.78f, s * 1.15f}, 0.04f, world, surface);
        Part(sphere_, Xf().S(0.38f, 0.2f, 0.13f).T(-0.3f, -0.82f, s * 1.15f), world, body, 0.4f);
        Part(cylinder_, Xf().S(0.22f, 0.1f, 0.22f).T(0, -0.05f, 0).RX(PI / 2).T(-0.3f, -0.83f, s * 1.15f), world, tire, 0.1f);
    }
    if (!B) return;
    Matrix finFrame = Xf().RZ(30.0f * DEG2RAD).T(-4.7f, 0.4f, 0.0f);
    Part(cube_, Xf().S(1.0f, 1.3f, 0.1f).T(0, 0.65f, 0).Then(finFrame), world, body, 0.4f);
    Part(cube_, Xf().S(0.4f, 1.15f, 0.08f).T(-0.2f, 0.6f, 0).RY(a.rudder).T(-0.5f, 0, 0).Then(finFrame), world, accent, 0.3f);
    // Носовая стойка
    PartBetween({1.4f, -0.4f, 0}, {1.4f, -0.83f, 0}, 0.04f, world, surface);
    Part(cylinder_, Xf().S(0.2f, 0.09f, 0.2f).T(0, -0.045f, 0).RX(PI / 2).RY(-a.noseSteer).T(1.4f, -0.85f, 0), world, tire, 0.1f);
    // Винт: на больших оборотах — полупрозрачный диск, на малых — видны лопасти.
    Part(cone_, Xf().S(0.18f, 0.35f, 0.18f).RZ(-PI / 2).T(2.4f, 0, 0), world, Color{220, 220, 220, 255}, 0.7f);
    float rpm = a.engines[0].n1;
    if (rpm > 45.0f) {
        Part(cylinder_, Xf().S(0.95f, 0.02f, 0.95f).RZ(-PI / 2).T(2.45f, 0, 0), world, Color{60, 60, 60, 60}, 0.0f);
    } else {
        static float angle = 0;
        angle += rpm * 0.02f;
        Part(cube_, Xf().S(0.05f, 1.9f, 0.14f).RX(angle).T(2.45f, 0, 0), world, dark, 0.2f);
    }
    (void)time;
}

void Scene::DrawAirliner(const Aircraft& a, const Matrix& world, int mask)
{
    const Color body{240, 242, 246, 255}, belly{40, 70, 140, 255}, glass{28, 38, 54, 255};
    const Color wing = Iced({200, 204, 212, 255}, a.ice), surface{176, 182, 192, 255}, dark{38, 40, 44, 255}, tire{25, 25, 25, 255};
    const bool B = mask & PM_BODY;

    if (B) {
    Part(cylinder_, Xf().S(2.0f, 28.0f, 2.0f).T(0, -14.0f, 0).RZ(-PI / 2), world, body, 0.5f);
    Part(sphere_, Xf().S(4.6f, 2.0f, 2.0f).T(14.0f, 0, 0), world, body, 0.5f);
    Part(sphere_, Xf().S(6.5f, 1.6f, 1.6f).T(-14.0f, 0.4f, 0), world, body, 0.5f);
    Part(cube_, Xf().S(27.0f, 0.9f, 4.03f).T(0, -1.25f, 0), world, belly, 0.4f);           // синее «брюхо»
    Part(cube_, Xf().S(25.0f, 0.28f, 4.04f).T(-0.5f, 0.55f, 0), world, glass, 0.8f);      // ряд окон
    Part(sphere_, Xf().S(1.6f, 0.55f, 1.5f).T(16.6f, 0.65f, 0), world, glass, 0.9f);     // окна кабины
    }

    const Vector3 root{-1.0f, -1.3f, 0.0f};
    for (float s : {-1.0f, 1.0f}) {
        if (WingOn(mask, s)) {
        Matrix wingFrame = Xf().RX(-s * 5.0f * DEG2RAD).RY(-s * 25.0f * DEG2RAD).T(root);
        Part(cube_, Xf().S(5.2f, 0.45f, 17.0f).T(0, 0, s * 8.5f).Then(wingFrame), world, wing, 0.4f);
        float ail = s > 0 ? -a.aileron : a.aileron;
        Part(cube_, Xf().S(1.0f, 0.2f, 4.0f).T(-0.5f, 0, 0).RZ(ail).T(-2.6f, 0, s * 13.5f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(1.4f, 0.22f, 8.0f).T(-0.7f, 0, 0).RZ(a.flaps * DEG2RAD).T(-2.6f, 0, s * 6.5f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(1.4f, 0.08f, 6.0f).T(-0.7f, 0, 0).RZ(-a.speedbrake * 45.0f * DEG2RAD).T(-0.6f, 0.26f, s * 7.0f).Then(wingFrame),
             world, surface, 0.3f);
        Part(cube_, Xf().S(0.4f, 1.6f, 0.08f).T(-1.2f, 0.8f, s * 17.0f).Then(wingFrame), world, wing, 0.4f);   // законцовка
        // Двигатели под крылом
        Part(cylinder_, Xf().S(1.05f, 4.4f, 1.05f).T(0, -2.2f, 0).RZ(-PI / 2).T(2.0f, -2.2f, s * 5.75f), world, Color{214, 218, 226, 255}, 0.6f);
        Part(cylinder_, Xf().S(0.85f, 0.05f, 0.85f).RZ(-PI / 2).T(4.22f, -2.2f, s * 5.75f), world, dark, 0.2f);
        Part(cube_, Xf().S(3.2f, 0.9f, 0.35f).T(1.0f, -1.45f, s * 5.75f), world, wing, 0.4f);
        if (a.reverser > 0.05f)
            Part(cylinder_, Xf().S(1.12f, 0.8f * a.reverser, 1.12f).T(0, -0.4f, 0).RZ(-PI / 2).T(0.6f, -2.2f, s * 5.75f), world, dark, 0.3f);
        }
        if (!(mask & PM_TAIL)) continue;
        // Стабилизатор
        Matrix stabFrame = Xf().RX(-s * 6.0f * DEG2RAD).RY(-s * 30.0f * DEG2RAD).T(-16.0f, 0.6f, 0.0f);
        Part(cube_, Xf().S(3.2f, 0.3f, 6.2f).T(0, 0, s * 3.1f).Then(stabFrame), world, wing, 0.4f);
        Part(cube_, Xf().S(1.0f, 0.18f, 5.6f).T(-0.5f, 0, 0).RZ(-a.elevator).T(-1.6f, 0, s * 3.1f).Then(stabFrame), world, surface, 0.3f);
    }
    if (!B) return;
    Matrix finFrame = Xf().RZ(35.0f * DEG2RAD).T(-15.0f, 1.6f, 0.0f);
    Part(cube_, Xf().S(4.6f, 6.2f, 0.4f).T(0, 3.1f, 0).Then(finFrame), world, belly, 0.4f);
    Part(cube_, Xf().S(1.4f, 5.6f, 0.3f).T(-0.7f, 2.9f, 0).RY(a.rudder).T(-2.3f, 0, 0).Then(finFrame), world, belly, 0.3f);

    if (a.gear > 0.03f) {
        struct Leg { float x, z, top; bool nose; };
        const Leg legs[3] = {{11.2f, 0.0f, -1.6f, true}, {-1.2f, -3.8f, -1.6f, false}, {-1.2f, 3.8f, -1.6f, false}};
        for (const Leg& l : legs) {
            float wy = ::Lerp(l.top - 0.2f, -3.55f, a.gear);
            Part(cylinder_, Xf().S(0.16f, l.top - wy, 0.16f).T(l.x, wy, l.z), world, Color{150, 152, 158, 255}, 0.6f);
            float steer = l.nose ? -a.noseSteer : 0.0f;
            float r = l.nose ? 0.38f : 0.55f;
            for (float w : {-0.4f, 0.4f})
                Part(cylinder_, Xf().S(r, 0.32f, r).T(0, -0.16f, 0).RX(PI / 2).T(0, 0, w).RY(steer).T(l.x, -4.0f + r, l.z), world, tire, 0.1f);
        }
    }
}

void Scene::DrawAircraftModel(const Aircraft& a, float time, bool self)
{
    const AircraftType& t = a.Type();
    const Matrix world = MatrixMultiply(QuaternionToMatrix(a.rot), MatrixTranslate(a.pos.x, a.pos.y, a.pos.z));
    auto drawParts = [&](const Matrix& m, int mask) {
        switch (t.kind) {
        case AircraftKind::Prop: DrawProp(a, m, time, mask); break;
        case AircraftKind::Airliner: DrawAirliner(a, m, mask); break;
        default: DrawLightJet(a, m, mask); break;
        }
    };
    int mask = PM_BODY;
    if (!a.lost[(int)Part::WingL]) mask |= PM_WING_L;
    if (!a.lost[(int)Part::WingR]) mask |= PM_WING_R;
    if (!a.lost[(int)Part::Tail]) mask |= PM_TAIL;
    if (self) drawParts(world, mask);
    // Оторванные части: та же геометрия, но со своим положением и вращением вокруг центра куска.
    const int partMask[] = {PM_WING_L, PM_WING_R, PM_TAIL};
    for (const Debris& d : a.debris) {
        if (!d.active) continue;
        Matrix m = MatrixMultiply(MatrixTranslate(-d.center.x, -d.center.y, -d.center.z),
                                  MatrixMultiply(QuaternionToMatrix(d.rot), MatrixTranslate(d.pos.x, d.pos.y, d.pos.z)));
        drawParts(m, partMask[(int)d.part]);
    }

    // Аэронавигационные огни, маячок, стробы и фары.
    Vector3 wl = t.contacts[WING_L], wr = t.contacts[WING_R];
    Vector3 tail = t.contacts[TAIL];
    Vector3 pl = a.ToWorld({wl.x + 0.2f, wl.y, wl.z - 0.05f}), pr = a.ToWorld({wr.x + 0.2f, wr.y, wr.z + 0.05f});
    Vector3 pt = a.ToWorld({tail.x - 0.9f, tail.y + 0.8f, 0});
    float k = t.span / 15.9f;
    const bool hasL = !a.lost[(int)Part::WingL], hasR = !a.lost[(int)Part::WingR];
    if (a.crashed || !self) return;
    if (hasL) DrawSphere(pl, 0.16f * k, RED);
    if (hasR) DrawSphere(pr, 0.16f * k, GREEN);
    DrawSphere(pt, 0.14f * k, WHITE);
    if (hasL) Glow(pl, Color{255, 40, 40, 255}, 1.6f * k);
    if (hasR) Glow(pr, Color{40, 255, 80, 255}, 1.6f * k);
    Glow(pt, Color{255, 255, 255, 200}, 1.4f * k);
    if (fmodf(time, 1.0f) < 0.12f) {
        Vector3 top = a.ToWorld({-0.5f * k, t.contacts[BELLY].y * -1.0f + 0.05f, 0});
        DrawSphere(top, 0.25f * k, Color{255, 40, 40, 255});
        Glow(top, Color{255, 40, 40, 255}, 4.0f * k);
    }
    float strobe = fmodf(time + 0.4f, 1.3f);
    if (strobe < 0.05f || (strobe > 0.12f && strobe < 0.17f)) {
        if (hasL) Glow(pl, WHITE, 8.0f * k);
        if (hasR) Glow(pr, WHITE, 8.0f * k);
    }
    if (a.gear > 0.99f) {   // посадочные фары на стойке/в крыле
        Vector3 ll = a.ToWorld({t.contacts[NOSE_WHEEL].x, t.contacts[NOSE_WHEEL].y + 0.6f, 0});
        Glow(ll, Color{255, 245, 220, 255}, 3.0f * k);
    }
}

void Scene::DrawShadow(const Aircraft& a, const Terrain& t)
{
    float agl = a.pos.y - t.SurfaceHeight(a.pos.x, a.pos.z);
    if (agl > 400.0f || env_.night) return;
    float light = Clampf(Vector3Length(env_.sunLight) * 1.4f, 0.0f, 1.0f);
    unsigned char alpha = (unsigned char)(110.0f * light * (1.0f - SmoothStep(20.0f, 400.0f, agl)));
    Color c{0, 0, 0, alpha};
    const AircraftType& ty = a.Type();
    float nose = ty.contacts[NOSE_CONE].x, tailX = ty.contacts[TAIL].x - 0.5f, half = fabsf(ty.contacts[WING_R].z);
    float fw = ty.span * 0.06f, ch = ty.chord;
    float sweep = ty.kind == AircraftKind::Prop ? 0.0f : half * 0.25f;

    auto proj = [&](float x, float z) {
        Vector3 p = a.ToWorld({x, 0, z});
        return Vector3{p.x, t.SurfaceHeight(p.x, p.z) + 0.4f, p.z};
    };
    auto quad = [&](Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3) {
        DrawTriangle3D(p0, p1, p2, c); DrawTriangle3D(p0, p2, p1, c);
        DrawTriangle3D(p0, p2, p3, c); DrawTriangle3D(p0, p3, p2, c);
    };
    quad(proj(nose, -fw), proj(nose, fw), proj(tailX, fw * 0.7f), proj(tailX, -fw * 0.7f));
    for (float s : {-1.0f, 1.0f}) {
        if (!a.lost[s < 0 ? (int)Part::WingL : (int)Part::WingR])
            quad(proj(ch * 0.6f, 0), proj(ch * 0.4f - sweep, s * half), proj(-ch * 0.4f - sweep, s * half), proj(-ch * 0.7f, 0));
        if (!a.lost[(int)Part::Tail]) quad(proj(tailX + ch * 0.6f, 0), proj(tailX + ch * 0.1f, s * half * 0.38f), proj(tailX - ch * 0.4f, s * half * 0.38f), proj(tailX, 0));
    }
}

void Scene::DrawCrashFx(const Aircraft& a)
{
    if (crashAge < 0 || !a.crashFire) return;
    float t = crashAge;
    float k = a.Type().span / 15.9f;
    for (int i = 0; i < 14; ++i) {
        float f = (float)i / 14.0f;
        float rise = t * (6.0f + 10.0f * f) * k;
        Vector3 c = Vector3Add(a.pos, {sinf(i * 2.4f) * 6.0f * f * k, rise, cosf(i * 1.7f) * 6.0f * f * k});
        float r = fminf(3.0f + 4.0f * f + t * 2.0f, 14.0f + 6.0f * f) * k;
        float fire = fmaxf(0.0f, 1.0f - t * 0.25f - f * 0.5f);
        unsigned char alpha = (unsigned char)(fmaxf(0.0f, 1.0f - t / 20.0f) * 170.0f);
        Color smoke{40, 38, 36, alpha};
        Color flame{255, (unsigned char)(120 + 100 * f), 30, (unsigned char)(220 * fire)};
        DrawSphere(c, r, fire > 0.05f ? flame : smoke);
        if (fire > 0.05f) DrawSphere(Vector3Add(c, {0, r * 1.5f, 0}), r * 1.2f, smoke);
    }
}

void Scene::Draw(const Aircraft& a, const Terrain& t, CamMode mode, float time, Vector3 wind, const Effects& fx, bool showAircraft)
{
    // Над сплошной облачностью — голубое небо и солнце, внутри облаков — белая мгла.
    float camAlt = camera.position.y;
    float density = env_.FogDensity(camAlt);
    Color fogC = env_.fogColor;
    if (env_.overcast && camAlt > env_.cloudBase && camAlt < env_.cloudTop)
        fogC = env_.night ? Color{30, 32, 38, 255} : Color{205, 208, 214, 255};
    Vector3 fog{fogC.r / 255.0f, fogC.g / 255.0f, fogC.b / 255.0f};
    SetShaderValue(lit_, locFog_, &fog, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit_, locFogDensity_, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit_, locView_, &camera.position, SHADER_UNIFORM_VEC3);
    // Посадочная фара: включена с выпущенным шасси.
    const AircraftType& ty = a.Type();
    float llOn = (showAircraft && a.gear > 0.99f && !a.crashed && (env_.dusk || env_.overcast)) ? 1.0f : 0.0f;
    Vector3 llPos = a.ToWorld({ty.contacts[NOSE_WHEEL].x, ty.contacts[NOSE_WHEEL].y + 0.6f, 0});
    Vector3 llDir = Vector3Normalize(Rotate({cosf(4 * DEG2RAD), -sinf(4 * DEG2RAD), 0}, a.rot));
    SetShaderValue(lit_, locLlOn_, &llOn, SHADER_UNIFORM_FLOAT);
    SetShaderValue(lit_, locLlPos_, &llPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(lit_, locLlDir_, &llDir, SHADER_UNIFORM_VEC3);

    bool inCloud = env_.overcast && camAlt > env_.cloudBase && camAlt < env_.cloudTop;
    if (inCloud) ClearBackground(fogC);

    BeginMode3D(camera);
    if (!inCloud) {
        DrawSky();
        fx.DrawSkyObjects(camera, env_);
    }
    SetSpecular(0.0f);
    t.Draw();
    float drawDist = Clampf(env_.visibility * 1.3f, 2500.0f, 14000.0f);
    if (env_.overcast && camAlt >= env_.cloudTop) drawDist = 0.0f;   // под облаками ничего не видно
    scenery_.Draw(camera.position, drawDist, env_.dusk);
    if (env_.dusk && drawDist > 0)
        for (const Vector3& p : scenery_.StreetLights())
            if (Vector3Distance(p, camera.position) < fminf(drawDist, 9000.0f)) Glow(p, Color{255, 200, 120, 200}, 4.0f, 0.0012f);
    for (int i = 0; i < world::AirportCount(); ++i) DrawAirport(i, time, wind);
    rlDrawRenderBatchActive();
    fx.DrawSkidMarks(camera, fminf(3500.0f, env_.visibility * 0.7f) * (env_.FogDensity(camAlt) > 0.01f ? 0.1f : 1.0f));
    if (showAircraft) DrawAircraftModel(a, time, mode != CamMode::Cockpit);
    rlDrawRenderBatchActive();
    if (showAircraft) DrawShadow(a, t);
    rlDrawRenderBatchActive();
    DrawGlows();
    fx.DrawClouds(camera, env_);
    fx.DrawParticles(camera);
    fx.DrawRain3D(camera, env_);
    if (a.crashed) DrawCrashFx(a);
    EndMode3D();
}
