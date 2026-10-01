#include "Terrain.h"

#include <cstdlib>
#include <cstring>

namespace {

unsigned Hash(int x, int y, unsigned seed)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float Lattice(int x, int y, unsigned seed) { return (Hash(x, y, seed) & 0xFFFFFF) / 8388607.5f - 1.0f; }

// Значение шума в [-1, 1] с пятой степенью сглаживания.
float ValueNoise(float x, float y, unsigned seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
    float uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    float a = Lattice(xi, yi, seed), b = Lattice(xi + 1, yi, seed);
    float c = Lattice(xi, yi + 1, seed), d = Lattice(xi + 1, yi + 1, seed);
    return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
}

float Fbm(float x, float y, unsigned seed, int octaves)
{
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * ValueNoise(x, y, seed + o * 101u);
        norm += amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return sum / norm;
}

// Гребни: 1 - |шум|, каждая октава модулируется предыдущей.
float Ridged(float x, float y, unsigned seed, int octaves)
{
    float sum = 0, amp = 0.5f, norm = 0, prev = 1.0f;
    for (int o = 0; o < octaves; ++o) {
        float n = 1.0f - fabsf(ValueNoise(x, y, seed + 7u + o * 131u));
        n *= n;
        sum += amp * n * prev;
        norm += amp;
        prev = n;
        x *= 2.1f; y *= 2.1f; amp *= 0.5f;
    }
    return sum / norm;
}

// Расстояние от точки до прямоугольника с центром в 0 и полуразмерами (hx, hz).
float DistToRect(float x, float z, float hx, float hz)
{
    float dx = fmaxf(fabsf(x) - hx, 0.0f), dz = fmaxf(fabsf(z) - hz, 0.0f);
    return sqrtf(dx * dx + dz * dz);
}

Color Mix(Color a, Color b, float t)
{
    t = fs::Clampf(t, 0, 1);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), 255};
}

} // namespace

float Terrain::RawHeight(float x, float z) const
{
    using fs::SmoothStep;
    const float ext = HalfExtent();

    float hills = 140.0f + 200.0f * Fbm(x / 6500.0f, z / 6500.0f, seed_, 5);

    float ridge = Ridged(x / 5200.0f + 13.1f, z / 5200.0f - 7.7f, seed_ + 17u, 6);
    float rangeMask = 0.55f + 0.45f * Fbm(x / 14000.0f - 3.0f, z / 14000.0f + 5.0f, seed_ + 3u, 3) * 2.0f;
    float distAirport = DistToRect(x, z, kRunwayHalfLen, 300.0f);
    float mountainMask = SmoothStep(3000.0f, 9000.0f, distAirport);
    // Коридор вдоль оси ВПП: заход и вылет без гор прямо по курсу.
    mountainMask *= Lerp(0.12f, 1.0f, SmoothStep(1800.0f, 5000.0f, fabsf(z)));
    float mountains = powf(ridge, 2.0f) * 2300.0f * fs::Clampf(rangeMask, 0.15f, 1.3f) * mountainMask;

    float h = hills + mountains;

    // Береговая линия: к краям карты рельеф уходит под воду.
    float r = sqrtf(x * x + z * z) / ext;
    r += 0.08f * Fbm(x / 3000.0f, z / 3000.0f, seed_ + 9u, 3);
    h = h * (1.0f - SmoothStep(0.62f, 0.93f, r)) - 260.0f * SmoothStep(0.78f, 1.0f, r);

    // Ровная площадка аэродрома.
    float w = SmoothStep(250.0f, 2200.0f, DistToRect(x, z, kRunwayHalfLen + 400.0f, 260.0f));
    return Lerp(kAirportElev, h, w);
}

void Terrain::Generate(unsigned seed)
{
    seed_ = seed;
    n_ = 513;
    cell_ = 80.0f;
    origin_ = -0.5f * cell_ * (n_ - 1);
    h_.assign((size_t)n_ * n_, 0.0f);
    for (int j = 0; j < n_; ++j)
        for (int i = 0; i < n_; ++i)
            h_[(size_t)j * n_ + i] = RawHeight(origin_ + i * cell_, origin_ + j * cell_);
}

float Terrain::GroundHeight(float x, float z) const
{
    float gx = (x - origin_) / cell_, gz = (z - origin_) / cell_;
    if (gx < 0 || gz < 0 || gx >= n_ - 1 || gz >= n_ - 1) return -300.0f; // открытое море
    int i = (int)gx, j = (int)gz;
    float fx = gx - i, fz = gz - j;
    float a = At(i, j), b = At(i + 1, j), c = At(i, j + 1), d = At(i + 1, j + 1);
    // Та же диагональ, что и в сетке: треугольники (a, c, b) и (b, c, d).
    if (fx + fz <= 1.0f) return a + (b - a) * fx + (c - a) * fz;
    return d + (c - d) * (1.0f - fx) + (b - d) * (1.0f - fz);
}

Vector3 Terrain::HighestPoint() const
{
    size_t best = 0;
    for (size_t k = 1; k < h_.size(); ++k)
        if (h_[k] > h_[best]) best = k;
    return Vector3{origin_ + (best % n_) * cell_, h_[best], origin_ + (best / n_) * cell_};
}

Vector3 Terrain::Normal(float x, float z) const
{
    const float e = cell_ * 0.5f;
    float hl = SurfaceHeight(x - e, z), hr = SurfaceHeight(x + e, z);
    float hd = SurfaceHeight(x, z - e), hu = SurfaceHeight(x, z + e);
    return Vector3Normalize(Vector3{hl - hr, 2.0f * e, hd - hu});
}

void Terrain::BuildMeshes(Shader litShader)
{
    const int cellsPerChunk = 64;                 // 65×65 вершин < 65535 (индексы unsigned short)
    const int chunksPerSide = (n_ - 1) / cellsPerChunk;
    const Color sand{194, 178, 128, 255}, grass{86, 125, 58, 255}, grassDry{128, 140, 74, 255};
    const Color forest{48, 84, 42, 255}, rock{112, 104, 96, 255}, rockDark{82, 76, 72, 255};
    const Color snow{238, 240, 245, 255}, seabed{150, 140, 110, 255}, airfield{104, 142, 70, 255};

    auto vertexNormal = [&](int i, int j) {
        int il = i > 0 ? i - 1 : i, ir = i < n_ - 1 ? i + 1 : i;
        int jd = j > 0 ? j - 1 : j, ju = j < n_ - 1 ? j + 1 : j;
        float dx = (At(ir, j) - At(il, j)) / ((ir - il) * cell_);
        float dz = (At(i, ju) - At(i, jd)) / ((ju - jd) * cell_);
        return Vector3Normalize(Vector3{-dx, 1.0f, -dz});
    };

    for (int cz = 0; cz < chunksPerSide; ++cz) {
        for (int cx = 0; cx < chunksPerSide; ++cx) {
            const int vn = cellsPerChunk + 1;
            Mesh m{};
            m.vertexCount = vn * vn;
            m.triangleCount = cellsPerChunk * cellsPerChunk * 2;
            m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
            m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
            m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
            m.indices = (unsigned short*)MemAlloc(m.triangleCount * 3 * sizeof(unsigned short));

            for (int lj = 0; lj < vn; ++lj) {
                for (int li = 0; li < vn; ++li) {
                    int i = cx * cellsPerChunk + li, j = cz * cellsPerChunk + lj;
                    int v = lj * vn + li;
                    float x = origin_ + i * cell_, z = origin_ + j * cell_, h = At(i, j);
                    Vector3 nrm = vertexNormal(i, j);
                    m.vertices[v * 3 + 0] = x;
                    m.vertices[v * 3 + 1] = h;
                    m.vertices[v * 3 + 2] = z;
                    m.normals[v * 3 + 0] = nrm.x;
                    m.normals[v * 3 + 1] = nrm.y;
                    m.normals[v * 3 + 2] = nrm.z;

                    float n1 = Fbm(x / 900.0f, z / 900.0f, seed_ + 51u, 3);
                    float n2 = Fbm(x / 260.0f, z / 260.0f, seed_ + 77u, 2);
                    float slope = 1.0f - nrm.y;
                    Color c;
                    if (h < 0.0f) c = seabed;
                    else if (h < 7.0f) c = sand;
                    else {
                        c = Mix(grass, grassDry, 0.5f + n1);
                        c = Mix(c, forest, fs::SmoothStep(0.05f, 0.35f, n2 + 0.3f * n1) * (1.0f - fs::SmoothStep(700, 1000, h)));
                        c = Mix(c, rock, fs::SmoothStep(0.12f, 0.30f, slope) + fs::SmoothStep(900, 1300, h + 120 * n1));
                        c = Mix(c, rockDark, fs::SmoothStep(0.35f, 0.6f, slope) * 0.6f);
                        c = Mix(c, snow, fs::SmoothStep(1450, 1650, h + 150 * n1) * (1.0f - fs::SmoothStep(0.45f, 0.7f, slope)));
                    }
                    if (DistToRect(x, z, kRunwayHalfLen + 400.0f, 260.0f) < 1.0f && fabsf(h - kAirportElev) < 0.01f)
                        c = Mix(airfield, grassDry, 0.25f + 0.5f * n2);
                    m.colors[v * 4 + 0] = c.r;
                    m.colors[v * 4 + 1] = c.g;
                    m.colors[v * 4 + 2] = c.b;
                    m.colors[v * 4 + 3] = 255;
                }
            }
            int k = 0;
            for (int lj = 0; lj < cellsPerChunk; ++lj) {
                for (int li = 0; li < cellsPerChunk; ++li) {
                    unsigned short a = (unsigned short)(lj * vn + li), b = (unsigned short)(a + 1);
                    unsigned short c = (unsigned short)(a + vn), d = (unsigned short)(c + 1);
                    m.indices[k++] = a; m.indices[k++] = c; m.indices[k++] = b;
                    m.indices[k++] = b; m.indices[k++] = c; m.indices[k++] = d;
                }
            }
            UploadMesh(&m, false);
            chunks_.push_back(m);
        }
    }

    // Море: большой квадрат на нулевой высоте, уходящий за горизонт.
    {
        const float s = 120000.0f;
        water_ = Mesh{};
        water_.vertexCount = 4;
        water_.triangleCount = 2;
        water_.vertices = (float*)MemAlloc(4 * 3 * sizeof(float));
        water_.normals = (float*)MemAlloc(4 * 3 * sizeof(float));
        water_.colors = (unsigned char*)MemAlloc(4 * 4);
        water_.indices = (unsigned short*)MemAlloc(6 * sizeof(unsigned short));
        const float vx[4] = {-s, s, -s, s}, vz[4] = {-s, -s, s, s};
        for (int v = 0; v < 4; ++v) {
            water_.vertices[v * 3 + 0] = vx[v];
            water_.vertices[v * 3 + 1] = 0.0f;
            water_.vertices[v * 3 + 2] = vz[v];
            water_.normals[v * 3 + 0] = 0;
            water_.normals[v * 3 + 1] = 1;
            water_.normals[v * 3 + 2] = 0;
            water_.colors[v * 4 + 0] = 38;
            water_.colors[v * 4 + 1] = 92;
            water_.colors[v * 4 + 2] = 140;
            water_.colors[v * 4 + 3] = 255;
        }
        const unsigned short idx[6] = {0, 2, 1, 1, 2, 3};
        memcpy(water_.indices, idx, sizeof(idx));
        UploadMesh(&water_, false);
    }

    material_ = LoadMaterialDefault();
    material_.shader = litShader;
    built_ = true;
}

void Terrain::Draw() const
{
    if (!built_) return;
    for (const Mesh& m : chunks_) DrawMesh(m, material_, MatrixIdentity());
    DrawMesh(water_, material_, MatrixIdentity());
}

void Terrain::Unload()
{
    if (!built_) return;
    for (Mesh& m : chunks_) UnloadMesh(m);
    chunks_.clear();
    UnloadMesh(water_);
    // Шейдер принадлежит вызывающему коду — не выгружаем его вместе с материалом.
    MemFree(material_.maps);
    built_ = false;
}
