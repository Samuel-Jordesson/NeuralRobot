#include "gl_util.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

std::string readTextFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[erro] nao foi possivel abrir %s\n", path.c_str());
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string dirOf(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? std::string(".") : path.substr(0, p);
}

static std::string preprocess(const std::string& path, int depth = 0) {
    std::string src = readTextFile(path);
    if (depth > 8) return src;
    std::stringstream in(src), out;
    std::string line;
    while (std::getline(in, line)) {
        size_t inc = line.find("#include");
        if (inc != std::string::npos) {
            size_t a = line.find('"', inc), b = line.find('"', a + 1);
            if (a != std::string::npos && b != std::string::npos) {
                out << preprocess(dirOf(path) + "/" + line.substr(a + 1, b - a - 1), depth + 1) << "\n";
                continue;
            }
        }
        out << line << "\n";
    }
    return out.str();
}

static GLuint compile(GLenum type, const std::string& src, const std::string& name) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] erro compilando %s:\n%s\n", name.c_str(), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::load(const std::string& vsPath, const std::string& fsPath) {
    GLuint vs = compile(GL_VERTEX_SHADER, preprocess(vsPath), vsPath);
    GLuint fs = compile(GL_FRAGMENT_SHADER, preprocess(fsPath), fsPath);
    if (!vs || !fs) return false;
    id_ = glCreateProgram();
    glAttachShader(id_, vs);
    glAttachShader(id_, fs);
    glLinkProgram(id_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(id_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] erro linkando %s + %s:\n%s\n", vsPath.c_str(), fsPath.c_str(), log);
        return false;
    }
    return true;
}

GLint Shader::loc(const char* name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    GLint l = glGetUniformLocation(id_, name);
    cache_[name] = l;
    return l;
}

void Shader::set(const char* n, int v) { glUniform1i(loc(n), v); }
void Shader::set(const char* n, float v) { glUniform1f(loc(n), v); }
void Shader::set(const char* n, const glm::vec2& v) { glUniform2fv(loc(n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const glm::vec3& v) { glUniform3fv(loc(n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const glm::vec4& v) { glUniform4fv(loc(n), 1, glm::value_ptr(v)); }
void Shader::set(const char* n, const glm::mat4& v) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(v)); }
void Shader::setMat4Array(const char* n, const glm::mat4* m, int count) {
    glUniformMatrix4fv(loc(n), count, GL_FALSE, glm::value_ptr(m[0]));
}

void Mesh::draw() const {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

static Mesh upload(const std::vector<float>& v, const std::vector<unsigned>& idx) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    m.count = (GLsizei)idx.size();
    return m;
}

Mesh makeCube() {
    std::vector<float> v;
    std::vector<unsigned> idx;
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        glm::vec3 n = normals[f];
        glm::vec3 u = std::fabs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 t = glm::cross(u, n);
        glm::vec3 b = glm::cross(n, t);
        unsigned base = (unsigned)(v.size() / 6);
        const float s[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (auto& c : s) {
            glm::vec3 p = 0.5f * (n + c[0] * t + c[1] * b);
            v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        }
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return upload(v, idx);
}

Mesh makeSphere(int slices, int stacks) {
    std::vector<float> v;
    std::vector<unsigned> idx;
    for (int i = 0; i <= stacks; ++i) {
        float phi = (float)M_PI * i / stacks;
        for (int j = 0; j <= slices; ++j) {
            float th = 2.0f * (float)M_PI * j / slices;
            glm::vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
            v.insert(v.end(), {n.x, n.y, n.z, n.x, n.y, n.z});
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            unsigned a = i * (slices + 1) + j, b = a + slices + 1;
            idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    return upload(v, idx);
}

Mesh makePlane(float h) {
    std::vector<float> v = {-h, 0, -h, 0, 1, 0, h, 0, -h, 0, 1, 0, h, 0, h, 0, 1, 0, -h, 0, h, 0, 1, 0};
    std::vector<unsigned> idx = {0, 2, 1, 0, 3, 2};
    return upload(v, idx);
}

GLuint makeTexture2D(const unsigned char* rgba, int w, int h, bool srgb) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLEW_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::fmin(8.0f, maxAniso));
    }
    return t;
}

GLuint makeSolidTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool srgb) {
    unsigned char px[4] = {r, g, b, a};
    return makeTexture2D(px, 1, 1, srgb);
}

// ---------------------------------------------------------------- fundo (vista)
namespace {
float vhash(int x, int y, unsigned s) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF);
}
float vnoise(float x, float y, unsigned s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = vhash(xi, yi, s), b = vhash(xi + 1, yi, s), c = vhash(xi, yi + 1, s), d = vhash(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
// relevo: fBm suave (morros) + cristas (montanhas) que crescem com a distância
float vistaHeight(float x, float z, float r0, float r1, unsigned s) {
    const float d = std::sqrt(x * x + z * z);
    const float k = std::clamp((d - r0) / (r1 - r0), 0.0f, 1.0f);
    float hills = 0.0f, amp = 1.0f, f = 1.0f / 90.0f;
    for (int o = 0; o < 5; ++o) { hills += (vnoise(x * f, z * f, s + o) - 0.5f) * amp; amp *= 0.5f; f *= 2.1f; }
    float ridge = 0.0f;
    amp = 1.0f;
    f = 1.0f / 260.0f;
    for (int o = 0; o < 5; ++o) {
        float n = 1.0f - std::fabs(vnoise(x * f, z * f, s + 17 + o) * 2.0f - 1.0f);
        ridge += n * n * amp;
        amp *= 0.5f;
        f *= 2.0f;
    }
    const float rise = std::min(1.0f, (d - r0) / 60.0f);          // nasce no nível do chão
    // perto: morros de 15-45 m; longe: serras arredondadas de até ~110 m (mata atlântica, nada de picos alpinos)
    const float h = (22.0f + 34.0f * hills) * (1.0f - k) + (30.0f + 55.0f * ridge) * k + 25.0f * k * k;
    return std::max(-2.0f, h) * rise * rise * (3 - 2 * rise) - 1.5f;
}
} // namespace

Mesh makeVista(float r0, float r1, unsigned seed) {
    const int nr = 140, na = 720; // anéis x fatias (mais denso perto: raio cresce ao quadrado)
    std::vector<float> v;
    v.reserve((size_t)(nr + 1) * na * 6);
    auto P = [&](float r, float a) {
        float x = std::cos(a) * r, z = std::sin(a) * r;
        return glm::vec3(x, vistaHeight(x, z, r0, r1, seed), z);
    };
    for (int i = 0; i <= nr; ++i) {
        float t = (float)i / nr;
        float r = r0 + (r1 - r0) * t * t;
        for (int j = 0; j < na; ++j) {
            float a = j * 6.2831853f / na;
            glm::vec3 p = P(r, a);
            glm::vec3 n = glm::normalize(glm::cross(P(r, a + 0.004f) - P(r, a - 0.004f), P(r - 1.5f, a) - P(r + 1.5f, a)));
            if (n.y < 0.0f) n = -n;
            v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        }
    }
    std::vector<unsigned> idx;
    for (int i = 0; i < nr; ++i)
        for (int j = 0; j < na; ++j) {
            unsigned a = i * na + j, b = i * na + (j + 1) % na, c = a + na, d = b + na;
            idx.insert(idx.end(), {a, b, c, b, d, c});
        }
    return upload(v, idx);
}

float vistaHeightAt(float x, float z, float r0, float r1, unsigned seed) { return vistaHeight(x, z, r0, r1, seed); }
