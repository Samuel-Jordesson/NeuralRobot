#include "qrcode.h"

#include <cstdint>

namespace {
// Multiplicação no corpo GF(256) do QR (polinômio 0x11D).
uint8_t gfMul(uint8_t a, uint8_t b) {
    int r = 0;
    for (int i = 7; i >= 0; --i) {
        r = (r << 1) ^ ((r >> 7) * 0x11D);
        r ^= ((b >> i) & 1) * a;
    }
    return (uint8_t)r;
}

// Bytes de correção Reed-Solomon de 'data' (grau 'n').
std::vector<uint8_t> reedSolomon(const std::vector<uint8_t>& data, int n) {
    std::vector<uint8_t> gen(n, 0); // gerador: (x - a^0)(x - a^1)...(x - a^(n-1)), sem o termo líder
    gen[n - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            gen[j] = gfMul(gen[j], root);
            if (j + 1 < n) gen[j] ^= gen[j + 1];
        }
        root = gfMul(root, 2);
    }
    std::vector<uint8_t> rem(n, 0);
    for (uint8_t b : data) {
        uint8_t f = b ^ rem[0];
        rem.erase(rem.begin());
        rem.push_back(0);
        for (int j = 0; j < n; ++j) rem[j] ^= gfMul(gen[j], f);
    }
    return rem;
}
} // namespace

bool QrCode::encode(const std::string& text) {
    // versões 1..5, correção L, um bloco só: bytes de dados e de correção por versão
    static const int kData[6] = {0, 19, 34, 55, 80, 108}, kEcc[6] = {0, 7, 10, 15, 20, 26};
    int ver = 0;
    for (int v = 1; v <= 5; ++v)
        if ((int)text.size() + 2 <= kData[v]) { ver = v; break; } // 4 bits modo + 8 de tamanho + texto
    if (!ver) return false;
    size = 17 + 4 * ver;
    dark.assign(size * size, false);
    std::vector<bool> fn(size * size, false);
    auto set = [&](int x, int y, bool d) { dark[y * size + x] = d; fn[y * size + x] = true; };

    // padrões fixos: tempo, localizadores, alinhamento, módulo escuro
    for (int i = 0; i < size; ++i) { set(6, i, i % 2 == 0); set(i, 6, i % 2 == 0); }
    auto finder = [&](int cx, int cy) {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                int d = std::max(std::abs(dx), std::abs(dy));
                set(x, y, d != 2 && d != 4);
            }
    };
    finder(3, 3);
    finder(size - 4, 3);
    finder(3, size - 4);
    if (ver >= 2) {
        int c = size - 7;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) set(c + dx, c + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
    // formato (correção L = 01, máscara 0) nas duas cópias
    {
        int data = (1 << 3) | 0, rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = ((data << 10) | rem) ^ 0x5412;
        auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; ++i) set(8, i, bit(i));
        set(8, 7, bit(6));
        set(8, 8, bit(7));
        set(7, 8, bit(8));
        for (int i = 9; i < 15; ++i) set(14 - i, 8, bit(i));
        for (int i = 0; i < 8; ++i) set(size - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; ++i) set(8, size - 15 + i, bit(i));
        set(8, size - 8, true);
    }

    // dados: modo byte, tamanho, texto, fim e enchimento
    std::vector<bool> bitsOut;
    auto put = [&](int v, int n) { for (int i = n - 1; i >= 0; --i) bitsOut.push_back((v >> i) & 1); };
    put(4, 4);
    put((int)text.size(), 8);
    for (unsigned char ch : text) put(ch, 8);
    const int capBits = kData[ver] * 8;
    put(0, std::min(4, capBits - (int)bitsOut.size()));
    while (bitsOut.size() % 8) bitsOut.push_back(false);
    std::vector<uint8_t> words;
    for (size_t i = 0; i < bitsOut.size(); i += 8) {
        int b = 0;
        for (int k = 0; k < 8; ++k) b = (b << 1) | (bitsOut[i + k] ? 1 : 0);
        words.push_back((uint8_t)b);
    }
    for (int pad = 0; (int)words.size() < kData[ver]; pad ^= 1) words.push_back(pad ? 0x11 : 0xEC);
    std::vector<uint8_t> ecc = reedSolomon(words, kEcc[ver]);
    words.insert(words.end(), ecc.begin(), ecc.end());

    // zigue-zague de baixo para cima, de duas em duas colunas, com a máscara 0 ((x + y) par inverte)
    size_t i = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size; ++vert)
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vert : vert;
                if (fn[y * size + x]) continue;
                bool b = i < words.size() * 8 && ((words[i >> 3] >> (7 - (i & 7))) & 1);
                ++i;
                dark[y * size + x] = b != ((x + y) % 2 == 0);
            }
    }
    return true;
}
