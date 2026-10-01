#include "Scene.h"
#include "Aircraft.h"
#include "Terrain.h"

#include "rlgl.h"

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

const char* kLitFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float specular;
out vec4 finalColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 base = fragColor.rgb * colDiffuse.rgb;
    float diff = max(dot(n, sunDir), 0.0);
    float skyLight = 0.5 + 0.5 * n.y;
    vec3 col = base * (0.28 + 0.22 * skyLight + 0.72 * diff);
    vec3 toEye = viewPos - fragPos;
    float dist = length(toEye);
    vec3 h = normalize(toEye / max(dist, 0.001) + sunDir);
    col += vec3(1.0, 0.97, 0.9) * specular * pow(max(dot(n, h), 0.0), 48.0);
    float f = 1.0 - exp(-pow(dist * fogDensity, 1.5));
    col = mix(col, fogColor, clamp(f, 0.0, 1.0));
    finalColor = vec4(col, fragColor.a * colDiffuse.a);
}
)";

const Vector3 kSunDir = Vector3Normalize({0.45f, 0.75f, -0.35f});
const Color kHaze{196, 212, 228, 255};
const Color kZenith{62, 112, 190, 255};

// Цепочка преобразований: каждое следующее применяется после предыдущего.
struct Xf {
    Matrix m = MatrixIdentity();
    Xf& S(float x, float y, float z) { m = MatrixMultiply(m, MatrixScale(x, y, z)); return *this; }
    Xf& T(float x, float y, float z) { m = MatrixMultiply(m, MatrixTranslate(x, y, z)); return *this; }
    Xf& RX(float a) { m = MatrixMultiply(m, MatrixRotateX(a)); return *this; }
    Xf& RY(float a) { m = MatrixMultiply(m, MatrixRotateY(a)); return *this; }
    Xf& RZ(float a) { m = MatrixMultiply(m, MatrixRotateZ(a)); return *this; }
    Xf& Then(const Matrix& o) { m = MatrixMultiply(m, o); return *this; }
    operator Matrix() const { return m; }
};

Mesh GenSkyDome()
{
    const int rings = 16, slices = 32;
    Mesh m{};
    m.vertexCount = (rings + 1) * (slices + 1);
    m.triangleCount = rings * slices * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
    m.indices = (unsigned short*)MemAlloc(m.triangleCount * 3 * sizeof(unsigned short));
    for (int r = 0; r <= rings; ++r) {
        float el = Lerp(-25.0f, 90.0f, (float)r / rings) * DEG2RAD;
        float t = el > 0 ? powf(sinf(el), 0.55f) : 0.0f;
        Color c{(unsigned char)Lerp(kHaze.r, kZenith.r, t), (unsigned char)Lerp(kHaze.g, kZenith.g, t),
                (unsigned char)Lerp(kHaze.b, kZenith.b, t), 255};
        if (el < 0) c = Color{(unsigned char)(kHaze.r * 0.85f), (unsigned char)(kHaze.g * 0.88f), (unsigned char)(kHaze.b * 0.9f), 255};
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

// Цифры разметки ВПП из «сегментов», как на семисегментном индикаторе.
const unsigned char kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

} // namespace

const char* CamModeName(CamMode m)
{
    switch (m) {
    case CamMode::Cockpit: return "COCKPIT";
    case CamMode::Chase: return "CHASE";
    case CamMode::Orbit: return "EXTERNAL";
    case CamMode::Tower: return "TOWER";
    default: return "";
    }
}

void Scene::Init()
{
    lit_ = LoadShaderFromMemory(kLitVS, kLitFS);
    lit_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lit_, "matModel");
    lit_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lit_, "matNormal");
    locSun_ = GetShaderLocation(lit_, "sunDir");
    locView_ = GetShaderLocation(lit_, "viewPos");
    locFog_ = GetShaderLocation(lit_, "fogColor");
    locFogDensity_ = GetShaderLocation(lit_, "fogDensity");
    locSpec_ = GetShaderLocation(lit_, "specular");

    SetShaderValue(lit_, locSun_, &kSunDir, SHADER_UNIFORM_VEC3);
    Vector3 fog{kHaze.r / 255.0f, kHaze.g / 255.0f, kHaze.b / 255.0f};
    SetShaderValue(lit_, locFog_, &fog, SHADER_UNIFORM_VEC3);
    float density = 1.0f / 30000.0f;
    SetShaderValue(lit_, locFogDensity_, &density, SHADER_UNIFORM_FLOAT);
    SetSpecular(0.0f);

    mat_ = LoadMaterialDefault();
    mat_.shader = lit_;
    unlit_ = LoadMaterialDefault();

    sphere_ = GenMeshSphere(1.0f, 14, 22);
    cube_ = GenMeshCube(1.0f, 1.0f, 1.0f);
    cylinder_ = GenMeshCylinder(1.0f, 1.0f, 16);
    cone_ = GenMeshCone(1.0f, 1.0f, 12);
    sky_ = GenSkyDome();

    camera.up = {0, 1, 0};
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
}

void Scene::Unload()
{
    UnloadMesh(sphere_);
    UnloadMesh(cube_);
    UnloadMesh(cylinder_);
    UnloadMesh(cone_);
    UnloadMesh(sky_);
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

void Scene::Box(Vector3 center, Vector3 size, Color c, float spec)
{
    Part(cube_, Xf().S(size.x, size.y, size.z).T(center.x, center.y, center.z), MatrixIdentity(), c, spec);
}

// ------------------------------------------------------------------ камеры

void Scene::ResetCamera(const Aircraft& a)
{
    chaseOffset_ = Vector3Add(Vector3Scale(a.Forward(), -28.0f), {0, 7.0f, 0});
    lookYaw_ = lookPitch_ = 0;
}

void Scene::UpdateCamera(const Aircraft& a, CamMode mode, float dt)
{
    // Мышь: в кабине ПКМ — оглядеться, снаружи ЛКМ/ПКМ — вращать, колесо — дистанция.
    Vector2 md = GetMouseDelta();
    bool dragging = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (mode != CamMode::Cockpit && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    float wheel = GetMouseWheelMove();

    if (a.crashed && (mode == CamMode::Cockpit || mode == CamMode::Chase)) {
        // После катастрофы — вид со стороны, чтобы камера не оказалась внутри огня.
        Vector3 desired{-70.0f, 35.0f, 40.0f};
        chaseOffset_ = Vector3Lerp(chaseOffset_, desired, fminf(dt * 1.5f, 1.0f));
        camera.position = Vector3Add(a.pos, chaseOffset_);
        camera.target = a.pos;
        camera.up = {0, 1, 0};
        camera.fovy = 60.0f;
        return;
    }

    switch (mode) {
    case CamMode::Cockpit: {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            lookYaw_ = Clampf(lookYaw_ - md.x * 0.25f, -150.0f, 150.0f);
            lookPitch_ = Clampf(lookPitch_ - md.y * 0.25f, -60.0f, 70.0f);
        } else {
            lookYaw_ = Lerp(lookYaw_, 0.0f, fminf(dt * 4.0f, 1.0f));
            lookPitch_ = Lerp(lookPitch_, 0.0f, fminf(dt * 4.0f, 1.0f));
        }
        Vector3 eye = a.ToWorld({5.55f, 0.72f, 0.0f});
        Vector3 dirLocal{cosf(lookPitch_ * DEG2RAD) * cosf(lookYaw_ * DEG2RAD), sinf(lookPitch_ * DEG2RAD),
                         -cosf(lookPitch_ * DEG2RAD) * sinf(lookYaw_ * DEG2RAD)};
        camera.position = eye;
        camera.target = Vector3Add(eye, Rotate(dirLocal, a.rot));
        camera.up = a.Up();
        camera.fovy = 68.0f;
        break;
    }
    case CamMode::Chase: {
        Vector3 desired = Vector3Add(Vector3Scale(a.Forward(), -28.0f), Vector3Scale(a.Up(), 6.0f));
        desired.y += 2.0f;
        chaseOffset_ = Vector3Lerp(chaseOffset_, desired, fminf(dt * 3.0f, 1.0f));
        camera.position = Vector3Add(a.pos, chaseOffset_);
        camera.target = Vector3Add(a.pos, Vector3Scale(a.Forward(), 12.0f));
        camera.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, a.Up(), 0.3f));
        camera.fovy = 60.0f;
        break;
    }
    case CamMode::Orbit: {
        if (dragging) {
            orbitYaw_ += md.x * 0.3f;
            orbitPitch_ = Clampf(orbitPitch_ + md.y * 0.3f, -80.0f, 85.0f);
        }
        orbitDist_ = Clampf(orbitDist_ * (1.0f - wheel * 0.1f), 12.0f, 400.0f);
        float yaw = orbitYaw_ * DEG2RAD, pitch = orbitPitch_ * DEG2RAD;
        Vector3 off{cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        camera.position = Vector3Add(a.pos, Vector3Scale(off, orbitDist_));
        camera.target = a.pos;
        camera.up = {0, 1, 0};
        camera.fovy = 55.0f;
        break;
    }
    case CamMode::Tower: {
        camera.position = {250.0f, Terrain::kAirportElev + 33.0f, -330.0f};
        camera.target = a.pos;
        camera.up = {0, 1, 0};
        float dist = Vector3Distance(camera.position, a.pos);
        camera.fovy = Clampf(2.0f * atanf(25.0f / fmaxf(dist, 1.0f)) * RAD2DEG, 1.5f, 70.0f);
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
    Vector3 sun = Vector3Add(camera.position, Vector3Scale(kSunDir, 38000.0f));
    DrawSphere(sun, 1600.0f, Color{255, 250, 225, 255});
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void Scene::DrawAirport(float time, Vector3 wind)
{
    const float E = Terrain::kAirportElev;
    const float L = Terrain::kRunwayHalfLen, W = Terrain::kRunwayHalfWidth;
    const Color asphalt{58, 60, 64, 255}, paint{232, 232, 228, 255}, taxi{70, 72, 76, 255}, yellow{220, 180, 40, 255};

    // Полоса и разметка (немного выше грунта, чтобы не мерцала).
    Box({0, E + 0.15f, 0}, {2 * L, 0.3f, 2 * W}, asphalt, 0.05f);
    Box({0, E + 0.09f, 0}, {2 * L + 60, 0.18f, 2 * W + 14}, Color{92, 96, 92, 255});
    const float yPaint = E + 0.33f;
    for (float x = -L + 200; x < L - 200; x += 60.0f) Box({x, yPaint, 0}, {30, 0.06f, 0.9f}, paint);
    for (float s : {-1.0f, 1.0f}) {
        Box({0, yPaint, s * (W - 1.2f)}, {2 * L - 20, 0.06f, 0.9f}, paint);
        for (int i = 0; i < 4; ++i) {   // «зебра» порога
            float z = 3.0f + i * 4.6f;
            Box({s * (L - 24), yPaint, z}, {30, 0.06f, 1.8f}, paint);
            Box({s * (L - 24), yPaint, -z}, {30, 0.06f, 1.8f}, paint);
        }
        // знаки точки прицеливания
        Box({s * (L - 400), yPaint, 8.0f}, {50, 0.06f, 7}, paint);
        Box({s * (L - 400), yPaint, -8.0f}, {50, 0.06f, 7}, paint);
        // зона приземления
        for (int k = 1; k <= 3; ++k) {
            Box({s * (L - 400 - k * 150.0f), yPaint, 9.0f}, {22, 0.06f, 1.8f}, paint);
            Box({s * (L - 400 - k * 150.0f), yPaint, -9.0f}, {22, 0.06f, 1.8f}, paint);
        }
    }
    // Номера полосы: «09» у западного торца, «27» у восточного.
    auto digit = [&](int d, Vector3 origin, Vector3 upDir, Vector3 rightDir) {
        const float h = 18.0f, w = 8.0f, t = 1.6f;
        struct Seg { float u, v; bool horiz; };
        const Seg segs[7] = {{h, 0, true}, {h * 0.75f, w * 0.5f, false}, {h * 0.25f, w * 0.5f, false}, {0, 0, true},
                             {h * 0.25f, -w * 0.5f, false}, {h * 0.75f, -w * 0.5f, false}, {h * 0.5f, 0, true}};
        for (int i = 0; i < 7; ++i) {
            if (!(kSegments[d] & (1 << i))) continue;
            Vector3 c = Vector3Add(origin, Vector3Add(Vector3Scale(upDir, segs[i].u), Vector3Scale(rightDir, segs[i].v)));
            Vector3 along = segs[i].horiz ? rightDir : upDir;
            float len = segs[i].horiz ? w + t : h * 0.5f + t;
            Vector3 size{fabsf(along.x) * len + fabsf(along.z) * t, 0.06f, fabsf(along.z) * len + fabsf(along.x) * t};
            Box({c.x, yPaint, c.z}, size, paint);
        }
    };
    digit(0, {-L + 60, 0, -6.5f}, {1, 0, 0}, {0, 0, 1});
    digit(9, {-L + 60, 0, 6.5f}, {1, 0, 0}, {0, 0, 1});
    digit(2, {L - 60, 0, 6.5f}, {-1, 0, 0}, {0, 0, -1});
    digit(7, {L - 60, 0, -6.5f}, {-1, 0, 0}, {0, 0, -1});

    // Рулёжка, перрон, постройки.
    Box({0, E + 0.12f, -95}, {20, 0.24f, 150}, taxi);
    Box({0, E + 0.27f, -95}, {0.5f, 0.06f, 150}, yellow);
    Box({0, E + 0.12f, -215}, {600, 0.24f, 100}, taxi);
    Box({-200, E + 9, -305}, {70, 18, 50}, Color{150, 158, 168, 255}, 0.2f);
    Box({-120, E + 7, -305}, {60, 14, 50}, Color{176, 172, 160, 255}, 0.2f);
    Box({80, E + 6, -300}, {110, 12, 40}, Color{205, 205, 200, 255}, 0.2f);
    Part(cylinder_, Xf().S(4, 28, 4).T(250, E, -330), MatrixIdentity(), Color{200, 200, 196, 255});
    Part(cylinder_, Xf().S(6.5f, 5, 6.5f).T(250, E + 28, -330), MatrixIdentity(), Color{40, 70, 90, 255}, 0.8f);
    Part(cylinder_, Xf().S(7, 0.8f, 7).T(250, E + 33, -330), MatrixIdentity(), Color{180, 180, 176, 255});

    // Ветроуказатель: «колдун» вытягивается по ветру.
    {
        Vector3 base{-L + 150, E, -60};
        Part(cylinder_, Xf().S(0.12f, 7, 0.12f).T(base.x, base.y, base.z), MatrixIdentity(), Color{220, 220, 220, 255});
        float ws = Vector3Length(wind);
        float lift = SmoothStep(0.0f, 12.0f, ws);
        Vector3 flat = ws > 0.1f ? Vector3Normalize({wind.x, 0, wind.z}) : Vector3{1, 0, 0};
        Vector3 dir = Vector3Normalize(Vector3Add(Vector3Scale(flat, 0.15f + lift), {0, -(1.0f - lift) - 0.08f, 0}));
        Matrix orient = QuaternionToMatrix(QuaternionFromVector3ToVector3({0, -1, 0}, dir));
        float flap = 0.05f * sinf(time * 7.0f) * lift;
        Part(cone_, Xf().S(0.55f, 4.0f, 0.55f).RX(PI).RZ(flap).Then(orient).T(base.x, base.y + 7, base.z),
             MatrixIdentity(), Color{240, 110, 30, 255});
    }

    // Огни: торцевые, боковые, огни приближения и PAPI (видны без освещения).
    for (float x = -L; x <= L + 1; x += 60.0f) {
        DrawCube({x, E + 0.5f, W + 1.5f}, 0.6f, 0.6f, 0.6f, Color{255, 245, 200, 255});
        DrawCube({x, E + 0.5f, -W - 1.5f}, 0.6f, 0.6f, 0.6f, Color{255, 245, 200, 255});
    }
    for (float z = -W; z <= W; z += 4.5f) {
        DrawCube({-L - 2, E + 0.5f, z}, 0.7f, 0.7f, 0.7f, Color{60, 255, 90, 255});
        DrawCube({L + 2, E + 0.5f, z}, 0.7f, 0.7f, 0.7f, Color{60, 255, 90, 255});
    }
    for (float s : {-1.0f, 1.0f}) {
        for (float d = 30.0f; d <= 720.0f; d += 30.0f) {
            bool flash = fmodf(time * 2.0f - d / 720.0f, 1.0f) < 0.08f;   // «бегущий огонь»
            Color c = flash ? WHITE : Color{255, 240, 190, 255};
            float size = flash ? 2.2f : 1.0f;
            for (float z = -3; z <= 3; z += 3) DrawCube({s * (L + d), E + 1.0f, z}, size, size, size, c);
            if (fabsf(d - 300.0f) < 1.0f)
                for (float z = -15; z <= 15; z += 3) DrawCube({s * (L + d), E + 1.0f, z}, 1.0f, 1.0f, 1.0f, c);
        }
        // PAPI: четыре огня слева от полосы; белый — выше своего угла, красный — ниже.
        const float thresholds[4] = {3.5f, 3.17f, 2.83f, 2.5f};
        float px = s * (L - 300.0f);
        for (int i = 0; i < 4; ++i) {
            Vector3 lp{px, E + 1.2f, s * (W + 10.0f + i * 9.0f)};
            Vector3 d = Vector3Subtract(camera.position, lp);
            float horiz = sqrtf(d.x * d.x + d.z * d.z);
            bool facing = (s < 0) ? d.x < 0 : d.x > 0;
            float ang = atan2f(d.y, horiz) * RAD2DEG;
            Color c = !facing ? Color{60, 60, 60, 255} : (ang > thresholds[i] ? Color{255, 255, 255, 255} : Color{255, 30, 30, 255});
            DrawCube(lp, 3.0f, 1.6f, 1.2f, c);
        }
    }
}

void Scene::DrawAircraftModel(const Aircraft& a, float time)
{
    const Matrix world = MatrixMultiply(QuaternionToMatrix(a.rot), MatrixTranslate(a.pos.x, a.pos.y, a.pos.z));
    const Color body{236, 238, 242, 255}, accent{196, 34, 46, 255}, glass{28, 38, 54, 255};
    const Color wing{208, 212, 218, 255}, surface{178, 184, 194, 255}, dark{40, 42, 46, 255}, tire{25, 25, 25, 255};

    // Фюзеляж
    Part(sphere_, Xf().S(7.6f, 0.95f, 0.95f), world, body, 0.5f);
    Part(sphere_, Xf().S(7.63f, 0.16f, 0.968f).T(0, -0.1f, 0), world, accent, 0.5f);
    Part(sphere_, Xf().S(3.2f, 0.7f, 0.6f).T(-5.2f, 0.35f, 0), world, body, 0.5f);
    Part(sphere_, Xf().S(1.7f, 0.6f, 0.8f).T(4.9f, 0.42f, 0), world, glass, 0.9f);
    for (int i = 0; i < 5; ++i)
        for (float s : {-1.0f, 1.0f})
            Part(cube_, Xf().S(0.38f, 0.3f, 0.05f).T(-2.2f + i * 1.1f, 0.25f, s * 0.915f), world, glass, 0.9f);

    // Крылья, элероны, закрылки, интерцепторы
    const Vector3 wingRoot{0.2f, -0.55f, 0.0f};
    for (float s : {-1.0f, 1.0f}) {   // s = +1 — правое крыло
        Matrix wingFrame = Xf().RX(-s * 3.0f * DEG2RAD).RY(-s * 12.0f * DEG2RAD).T(wingRoot.x, wingRoot.y, wingRoot.z);
        Part(cube_, Xf().S(2.3f, 0.22f, 7.9f).T(0, 0, s * 3.95f).Then(wingFrame), world, wing, 0.4f);
        float ail = s > 0 ? -a.aileron : a.aileron;
        Part(cube_, Xf().S(0.55f, 0.12f, 2.6f).T(-0.275f, 0, 0).RZ(ail).T(-1.15f, 0, s * 6.3f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(0.6f, 0.14f, 3.7f).T(-0.3f, 0, 0).RZ(a.flaps * DEG2RAD).T(-1.15f, 0, s * 2.95f).Then(wingFrame), world, surface, 0.3f);
        Part(cube_, Xf().S(0.7f, 0.06f, 2.4f).T(-0.35f, 0, 0).RZ(-a.speedbrake * 45.0f * DEG2RAD).T(-0.25f, 0.12f, s * 3.2f).Then(wingFrame),
             world, surface, 0.3f);
        // Мотогондолы
        Part(sphere_, Xf().S(2.0f, 0.6f, 0.6f).T(-3.6f, 0.65f, s * 1.65f), world, Color{214, 218, 224, 255}, 0.6f);
        Part(sphere_, Xf().S(0.12f, 0.47f, 0.47f).T(-1.66f, 0.65f, s * 1.65f), world, dark, 0.2f);
        Part(cube_, Xf().S(1.2f, 0.18f, 0.75f).T(-3.6f, 0.58f, s * 1.0f), world, body, 0.4f);
        // Горизонтальное оперение (Т-образное) и руль высоты
        Matrix stabFrame = Xf().RY(-s * 10.0f * DEG2RAD).T(-7.3f, 3.42f, 0.0f);
        Part(cube_, Xf().S(1.7f, 0.16f, 3.1f).T(0, 0, s * 1.55f).Then(stabFrame), world, wing, 0.4f);
        Part(cube_, Xf().S(0.6f, 0.1f, 2.9f).T(-0.3f, 0, 0).RZ(-a.elevator).T(-0.85f, 0, s * 1.6f).Then(stabFrame), world, surface, 0.3f);
    }
    // Киль и руль направления
    Matrix finFrame = Xf().RZ(25.0f * DEG2RAD).T(-6.2f, 0.7f, 0.0f);
    Part(cube_, Xf().S(2.4f, 3.0f, 0.24f).T(0, 1.5f, 0).Then(finFrame), world, body, 0.4f);
    Part(cube_, Xf().S(0.75f, 2.7f, 0.18f).T(-0.375f, 1.4f, 0).RY(a.rudder).T(-1.2f, 0, 0).Then(finFrame), world, accent, 0.3f);

    // Шасси (анимация уборки)
    if (a.gear > 0.03f) {
        struct Leg { float x, z, top; bool nose; };
        const Leg legs[3] = {{4.9f, 0.0f, -0.6f, true}, {-0.6f, -1.8f, -0.62f, false}, {-0.6f, 1.8f, -0.62f, false}};
        for (const Leg& l : legs) {
            float wy = Lerp(l.top - 0.1f, -1.55f, a.gear);
            float len = l.top - wy;
            Part(cylinder_, Xf().S(0.07f, len, 0.07f).T(l.x, wy, l.z), world, Color{150, 152, 158, 255}, 0.6f);
            float steer = l.nose ? -a.noseSteer : 0.0f;
            float r = l.nose ? 0.3f : 0.36f;
            Part(cylinder_, Xf().S(r, 0.24f, r).T(0, -0.12f, 0).RX(PI / 2).RY(steer).T(l.x, -1.9f + r, l.z), world, tire, 0.1f);
        }
    }

    // Аэронавигационные огни, маячок и стробы
    DrawSphere(a.ToWorld({-1.6f, -0.2f, -7.95f}), 0.16f, RED);
    DrawSphere(a.ToWorld({-1.6f, -0.2f, 7.95f}), 0.16f, GREEN);
    DrawSphere(a.ToWorld({-8.4f, 0.4f, 0.0f}), 0.14f, WHITE);
    float beacon = fmodf(time, 1.0f);
    if (beacon < 0.12f) {
        DrawSphere(a.ToWorld({-0.5f, 0.98f, 0}), 0.25f, Color{255, 40, 40, 255});
        DrawSphere(a.ToWorld({-0.5f, -0.98f, 0}), 0.25f, Color{255, 40, 40, 255});
    }
    float strobe = fmodf(time + 0.4f, 1.3f);
    if (strobe < 0.05f || (strobe > 0.12f && strobe < 0.17f)) {
        DrawSphere(a.ToWorld({-1.6f, -0.2f, -8.05f}), 0.45f, WHITE);
        DrawSphere(a.ToWorld({-1.6f, -0.2f, 8.05f}), 0.45f, WHITE);
    }
}

void Scene::DrawShadow(const Aircraft& a, const Terrain& t)
{
    float agl = a.pos.y - t.SurfaceHeight(a.pos.x, a.pos.z);
    if (agl > 400.0f) return;
    unsigned char alpha = (unsigned char)(110.0f * (1.0f - SmoothStep(20.0f, 400.0f, agl)));
    Color c{0, 0, 0, alpha};

    auto proj = [&](float x, float z) {
        Vector3 p = a.ToWorld({x, 0, z});
        return Vector3{p.x, t.SurfaceHeight(p.x, p.z) + 0.4f, p.z};
    };
    auto quad = [&](Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3) {
        DrawTriangle3D(p0, p1, p2, c); DrawTriangle3D(p0, p2, p1, c);
        DrawTriangle3D(p0, p2, p3, c); DrawTriangle3D(p0, p3, p2, c);
    };
    quad(proj(7.4f, -0.5f), proj(7.4f, 0.5f), proj(-7.6f, 0.4f), proj(-7.6f, -0.4f));
    for (float s : {-1.0f, 1.0f}) {
        quad(proj(1.3f, 0), proj(-0.4f, s * 7.9f), proj(-2.4f, s * 7.9f), proj(-1.0f, 0));
        quad(proj(-6.5f, 0), proj(-7.4f, s * 3.1f), proj(-8.6f, s * 3.1f), proj(-8.4f, 0));
    }
}

void Scene::DrawCrashFx(const Aircraft& a)
{
    if (crashAge < 0) return;
    float t = crashAge;
    for (int i = 0; i < 14; ++i) {
        float k = (float)i / 14.0f;
        float rise = t * (6.0f + 10.0f * k);
        Vector3 c = Vector3Add(a.pos, {sinf(i * 2.4f) * 6.0f * k, rise, cosf(i * 1.7f) * 6.0f * k});
        float r = fminf(3.0f + 4.0f * k + t * 2.0f, 14.0f + 6.0f * k);
        float fire = fmaxf(0.0f, 1.0f - t * 0.25f - k * 0.5f);
        unsigned char alpha = (unsigned char)(fmaxf(0.0f, 1.0f - t / 20.0f) * 170.0f);
        Color smoke{40, 38, 36, alpha};
        Color flame{255, (unsigned char)(120 + 100 * k), 30, (unsigned char)(220 * fire)};
        DrawSphere(c, r, fire > 0.05f ? flame : smoke);
        if (fire > 0.05f) DrawSphere(Vector3Add(c, {0, r * 1.5f, 0}), r * 1.2f, smoke);
    }
}

void Scene::Draw(const Aircraft& a, const Terrain& t, CamMode mode, float time, Vector3 wind)
{
    SetShaderValue(lit_, locView_, &camera.position, SHADER_UNIFORM_VEC3);

    BeginMode3D(camera);
    DrawSky();
    SetSpecular(0.0f);
    t.Draw();
    DrawAirport(time, wind);
    if (mode != CamMode::Cockpit) DrawAircraftModel(a, time);
    rlDrawRenderBatchActive();
    DrawShadow(a, t);
    if (a.crashed) DrawCrashFx(a);
    EndMode3D();
}
