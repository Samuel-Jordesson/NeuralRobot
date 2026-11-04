#pragma once
#include <GL/glew.h>

// Mata ao fundo (entre o mapa e os morros), do jeito dos jogos AAA para vegetação distante:
// "impostors" — cada árvore são dois planos cruzados com a imagem de uma árvore recortada por
// transparência. A imagem (4 árvores diferentes) é gerada aqui; milhares delas custam pouco.
class TreeWall {
public:
    // Árvores no anel de r0 a r1 metros em volta da origem, seguindo o relevo do fundo.
    // atlas = 4 fotos do modelo de árvore (2x2, de ângulos diferentes); aspect = largura/altura da foto.
    void build(float r0, float r1, unsigned seed, GLuint atlas, float aspect);
    bool built() const { return vao_ != 0; }
    void draw() const;
    GLuint texture() const { return tex_; }

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0, tex_ = 0;
    GLsizei count_ = 0;
};
