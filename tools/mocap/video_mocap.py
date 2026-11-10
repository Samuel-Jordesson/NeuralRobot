# Captura de movimento a partir de um vídeo (MediaPipe): corpo, mãos (dedos) e cabeça.
#
#   python video_mocap.py VIDEO MODELOS SAIDA.txt
#
# MODELOS = pasta com pose_landmarker_heavy.task, hand_landmarker.task e face_landmarker.task
# (https://storage.googleapis.com/mediapipe-models/...). A saída vai para o jogo:
#   ./ga --mocap-video SAIDA.txt anims/NOME.anim
#
# O que este passo arruma antes de ir para o jogo:
#  - eixos da câmera -> espaço do personagem (+X esquerda dele, +Y cima, +Z frente)
#  - câmera torta (celular de baixo para cima): o tronco médio vira vertical e de frente
#  - cada mão detectada vai para o pulso certo (pela posição na imagem), buracos curtos são
#    preenchidos e a confiança entra/sai devagar (sem estalo quando a mão some)
#  - suavização sem atraso (gaussiana para os dois lados no tempo), mais forte em ponto escondido
import sys, json
import numpy as np

LOOK_DOWN = -9.0  # graus: rosto médio (negativo = um pouco para cima, câmera abaixo do rosto)


def detect(video, models):
    import cv2, mediapipe as mp
    from mediapipe.tasks.python import vision as V, BaseOptions as B
    mk = lambda f: B(model_asset_path=f'{models}/{f}')
    pose = V.PoseLandmarker.create_from_options(V.PoseLandmarkerOptions(
        base_options=mk('pose_landmarker_heavy.task'), running_mode=V.RunningMode.VIDEO,
        min_pose_detection_confidence=0.3, min_tracking_confidence=0.3))
    hand = V.HandLandmarker.create_from_options(V.HandLandmarkerOptions(
        base_options=mk('hand_landmarker.task'), running_mode=V.RunningMode.VIDEO, num_hands=2,
        min_hand_detection_confidence=0.3, min_tracking_confidence=0.3))
    face = V.FaceLandmarker.create_from_options(V.FaceLandmarkerOptions(
        base_options=mk('face_landmarker.task'), running_mode=V.RunningMode.VIDEO,
        output_facial_transformation_matrixes=True, num_faces=1))
    cap = cv2.VideoCapture(video)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    frames, i = [], 0
    while True:
        ok, img = cap.read()
        if not ok:
            break
        ts = int(i * 1000 / fps)
        i += 1
        im = mp.Image(image_format=mp.ImageFormat.SRGB, data=cv2.cvtColor(img, cv2.COLOR_BGR2RGB))
        f = {'t': ts / 1000.0, 'hands': []}
        r = pose.detect_for_video(im, ts)
        if r.pose_world_landmarks:
            f['pose'] = [[p.x, p.y, p.z, p.visibility] for p in r.pose_world_landmarks[0]]
            f['pose2d'] = [[p.x, p.y, p.z] for p in r.pose_landmarks[0]]
        r = hand.detect_for_video(im, ts)
        for k in range(len(r.hand_landmarks)):
            f['hands'].append({'img': [[p.x, p.y, p.z] for p in r.hand_landmarks[k]],
                               'world': [[p.x, p.y, p.z] for p in r.hand_world_landmarks[k]]})
        r = face.detect_for_video(im, ts)
        if r.facial_transformation_matrixes:
            f['head'] = np.array(r.facial_transformation_matrixes[0])[:3, :3].tolist()
        frames.append(f)
    return fps, frames, cap.get(3) / max(1.0, cap.get(4))


def gauss(a, sigma, w=None):
    """Suaviza no tempo (eixo 0) sem atraso; w = peso por quadro (0 = ignora o quadro)."""
    n = len(a)
    if w is None:
        w = np.ones(n)
    r = int(3 * sigma) + 1
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    shp = (-1,) + (1,) * (a.ndim - 1)
    num = np.zeros_like(a, dtype=float)
    den = np.zeros(n)
    for o, kv in zip(range(-r, r + 1), k):
        idx = np.clip(np.arange(n) + o, 0, n - 1)
        num += kv * w[idx].reshape(shp) * a[idx]
        den += kv * w[idx]
    return num / np.maximum(den, 1e-9).reshape(shp)


def rot_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    v = np.cross(a, b); c = float(np.dot(a, b))
    if c > 0.999999:
        return np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx / (1 + c)


def quat_from_mat(m):
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1) * 2
        return np.array([(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s])
    i = int(np.argmax([m[0, 0], m[1, 1], m[2, 2]]))
    j, k = (i + 1) % 3, (i + 2) % 3
    s = np.sqrt(1 + m[i, i] - m[j, j] - m[k, k]) * 2
    q = np.zeros(4)
    q[i] = 0.25 * s
    q[j] = (m[j, i] + m[i, j]) / s
    q[k] = (m[k, i] + m[i, k]) / s
    q[3] = (m[k, j] - m[j, k]) / s
    return q


ARM_CHAINS = [(11, 13), (13, 15), (15, 17), (15, 19), (15, 21), (12, 14), (14, 16), (16, 18), (16, 20), (16, 22)]


def arms_from_image(world, img):
    """Braços: o ângulo na imagem é exato (é o que a câmera viu); o 3D do MediaPipe erra a
    profundidade com a câmera de baixo. Cada osso do braço fica com a direção da imagem no plano
    da tela e a profundidade (e o comprimento) do 3D."""
    out = world.copy()
    for a, b in ARM_CHAINS:
        w = world[b] - world[a]
        u = img[b] - img[a]
        lxy = np.hypot(w[0], w[1])
        if np.linalg.norm(u) > 1e-6:
            d = np.array([*(u / np.linalg.norm(u) * lxy), w[2]])
        else:
            d = w
        out[b] = out[a] + d
    return out


def process(fps, frames, aspect):
    global ASPECT
    ASPECT = aspect
    n = len(frames)
    C = np.diag([1.0, -1.0, -1.0])  # câmera (x direita da imagem, y baixo, z longe) -> personagem
    P = np.zeros((n, 33, 3)); vis = np.zeros((n, 33)); P2 = np.zeros((n, 33, 2)); has = np.zeros(n)
    for i, f in enumerate(frames):
        if 'pose' in f:
            a = np.array(f['pose'])
            P[i] = arms_from_image(a[:, :3], np.array(f['pose2d'])[:, :2] * np.array([ASPECT, 1.0])) @ C.T
            vis[i] = a[:, 3]; P2[i] = np.array(f['pose2d'])[:, :2]; has[i] = 1
    # quadros sem pose: repete o vizinho
    for i in range(n):
        if not has[i]:
            j = min(range(n), key=lambda j: abs(j - i) if has[j] else 1e9)
            P[i], vis[i], P2[i] = P[j], vis[j] * 0, P2[j]

    # câmera torta: tronco médio na vertical e ombros na horizontal (de frente para +Z)
    up = np.mean((P[:, 11] + P[:, 12]) / 2 - (P[:, 23] + P[:, 24]) / 2, axis=0)
    R = rot_between(up, np.array([0.0, 1.0, 0.0]))
    side = np.mean(P[:, 11] - P[:, 12], axis=0) @ R.T
    side[1] = 0
    R = rot_between(side, np.array([1.0, 0.0, 0.0])) @ R
    # o quadril escondido (fora da imagem) puxa o tronco para trás; o rosto diz o resto: quem olha
    # para o celular olha um pouco para baixo. Inclina tudo até o rosto ficar quase na horizontal.
    fw = []
    for f in frames:
        if 'head' in f:
            fw.append(R @ np.array(f['head']) @ np.array([0.0, 0.0, 1.0]))
    if fw:
        pitch = np.degrees(np.arcsin(np.clip(np.mean(fw, axis=0)[1], -1, 1)))  # + = olhando para cima
        a = np.radians(np.clip(pitch - LOOK_DOWN, 0.0, 25.0))
        R = np.array([[1, 0, 0], [0, np.cos(a), -np.sin(a)], [0, np.sin(a), np.cos(a)]]) @ R
    P = P @ R.T

    # suavização: ponto bem visto quase cru, ponto escondido bem mais suave
    out = np.zeros_like(P)
    for j in range(33):
        fine = gauss(P[:, j], 1.2, 0.05 + vis[:, j])
        rough = gauss(P[:, j], 4.0, 0.05 + vis[:, j])
        k = np.clip((vis[:, j] - 0.3) / 0.5, 0, 1)[:, None]
        out[:, j] = fine * k + rough * (1 - k)
    P = out

    # mãos: cada detecção vai para o pulso mais perto na imagem (15 = esquerdo, 16 = direito)
    Hw = np.zeros((2, n, 21, 3)); Hv = np.zeros((2, n))
    for i, f in enumerate(frames):
        cands = []
        for h in f['hands']:
            w0 = np.array(h['img'][0][:2])
            for s, lm in ((0, 15), (1, 16)):
                cands.append((np.linalg.norm(w0 - P2[i, lm]), s, h))
        sides, hands = set(), set()
        for d, s, h in sorted(cands, key=lambda c: c[0]):
            if d > 0.18 or s in sides or id(h) in hands:
                continue
            sides.add(s); hands.add(id(h))
            Hw[s, i] = np.array(h['world']) @ C.T @ R.T
            Hv[s, i] = 1.0
    for s in range(2):
        idx = np.nonzero(Hv[s])[0]
        if len(idx) == 0:
            continue
        # buracos curtos (até 0,25 s): interpola entre as detecções vizinhas
        for a, b in zip(idx[:-1], idx[1:]):
            if 1 < b - a <= int(0.25 * fps):
                for i in range(a + 1, b):
                    u = (i - a) / (b - a)
                    Hw[s, i] = Hw[s, a] * (1 - u) + Hw[s, b] * u
                    Hv[s, i] = 1.0
        filled = Hv[s].copy()
        for i in range(n):  # fora das detecções: repete a mais próxima (o peso vai a zero)
            if not filled[i]:
                j = idx[np.argmin(np.abs(idx - i))]
                Hw[s, i] = Hw[s, j]
        Hw[s] = gauss(Hw[s], 1.5, 0.02 + filled)
        Hv[s] = np.clip(gauss(filled, 0.08 * fps), 0, 1)

    # cabeça: matriz do rosto -> quaternion no espaço do personagem (mesma correção da câmera)
    Q = np.zeros((n, 4)); Qv = np.zeros(n)
    F = np.diag([1.0, 1.0, 1.0])  # rosto: x direita da imagem, y cima, z para a câmera = nosso espaço
    for i, f in enumerate(frames):
        if 'head' in f:
            Q[i] = quat_from_mat(R @ F @ np.array(f['head']) @ F.T); Qv[i] = 1
    for i in range(1, n):
        if np.dot(Q[i], Q[i - 1]) < 0:
            Q[i] = -Q[i]
    Q = gauss(Q, 2.0, 0.02 + Qv)
    Q /= np.linalg.norm(Q, axis=1, keepdims=True)
    Qv = np.clip(gauss(Qv, 0.1 * fps), 0, 1)
    return P, Hw, Hv, Q, Qv


def main():
    video, models, out = sys.argv[1:4]
    import os
    cache = out + '.deteccao.json'  # detecção é a parte lenta: refazer só o processamento é rápido
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(video):
        fps, frames, aspect = json.load(open(cache))
    else:
        fps, frames, aspect = detect(video, models)
        json.dump([fps, frames, aspect], open(cache, 'w'))
    P, Hw, Hv, Q, Qv = process(fps, frames, aspect)
    with open(out, 'w') as o:
        o.write(f'# mocap de video (GA): {video}\nfps {fps:g}\nframes {len(frames)}\n')
        for i, f in enumerate(frames):
            o.write(f"f {f['t']:.5f}\n")
            o.write('p ' + ' '.join(f'{v:.5f}' for v in P[i].ravel()) + '\n')
            for s in range(2):
                o.write(f'h {s} {Hv[s, i]:.3f} ' + ' '.join(f'{v:.5f}' for v in Hw[s, i].ravel()) + '\n')
            o.write(f'q {Qv[i]:.3f} ' + ' '.join(f'{v:.5f}' for v in Q[i]) + '\n')
    print(f'{len(frames)} quadros, mãos: esq {int((Hv[0] > 0.5).sum())} dir {int((Hv[1] > 0.5).sum())}')


if __name__ == '__main__':
    main()
