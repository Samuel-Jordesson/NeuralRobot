#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

std::string readTextFile(const std::string& path);

class Shader {
public:
    // Suporta '#include "arquivo"' relativo à pasta do shader.
    bool load(const std::string& vsPath, const std::string& fsPath);
    void use() const { glUseProgram(id_); }
    GLuint id() const { return id_; }

    void set(const char* name, int v);
    void set(const char* name, float v);
    void set(const char* name, const glm::vec2& v);
    void set(const char* name, const glm::vec3& v);
    void set(const char* name, const glm::vec4& v);
    void set(const char* name, const glm::mat4& v);
    void setMat4Array(const char* name, const glm::mat4* m, int count);

private:
    GLint loc(const char* name);
    GLuint id_ = 0;
    std::unordered_map<std::string, GLint> cache_;
};

// Geometria estática simples: posição + normal.
struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei count = 0;
    void draw() const;
};

Mesh makeCube();                    // cubo unitário centrado na origem
Mesh makeSphere(int slices, int stacks); // raio 1
Mesh makePlane(float halfSize);     // plano y = 0
// Fundo do cenário (estilo AAA): três anéis de relevo em volta do mapa — morros com mata perto,
// serras no meio, montanhas ao longe — de raio interno r0 até o externo r1 (m). x,y,z,nx,ny,nz + camada.
Mesh makeVista(float r0, float r1, unsigned seed);
float vistaHeightAt(float x, float z, float r0, float r1, unsigned seed); // altura do fundo no ponto (m)

GLuint makeTexture2D(const unsigned char* rgba, int w, int h, bool srgb);
GLuint makeSolidTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool srgb);
