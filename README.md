# GA — protótipo 3ª pessoa (C++ / OpenGL 3.3)

## Compilar e rodar
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/ga
```
Dependências (Ubuntu): `libglfw3-dev libglew-dev libglm-dev libassimp-dev`.

## Controles
| Tecla | Ação |
|---|---|
| W A S D | mover (relativo à câmera) |
| Shift | correr |
| Espaço | pular |
| E | pegar a arma da mesa (ele anda até a borda e pega com a mão) |
| 1 / 2 / 3 | pistola (cintura) / fuzil 1 / fuzil 2 (costas): saca; a mesma tecla guarda; outra tecla troca de arma |
| Botão direito | mirar (zoom no ombro + mira de ponto) |
| Botão esquerdo | atirar |
| G | soltar a arma |
| Q | cobertura: gruda na caixa/parede à frente (Q de novo ou puxar para trás sai); A/D anda colado; botão direito atira dela (baixa: levanta por cima; alta: espia pela quina) |
| Mouse / Scroll | girar câmera / zoom |
| F1 | ver pesos do rig |
| F2 | liga/desliga Foot IK |
| R | voltar ao início |
| Esc | menu de pausa com as opções gráficas |

## Arena
Em frente ao início (siga reto para +Z) fica uma arena murada de 26 x 26 m. A porta de madeira abre sozinha quando alguém chega perto, sempre para o lado oposto de quem vem, e fechada bloqueia passagem e tiros. Lá dentro há 5 soldados (`russian_soldier.glb`) com AK-47: quando te veem, avançam, andam de lado e atiram em rajadas. Os soldados correm para trás de coberturas baixas que os escondam de você, agacham e alternam entre esconder e levantar atirando; se você der a volta e eles ficarem expostos, trocam de lugar. Tiro na cabeça dá o triplo de dano. A vida volta sozinha depois de 5 s sem levar tiro. Se você morre, renasce no início e a arena recomeça. Arena limpa: nova onda em 8 s.

## Estrutura
- `src/skeleton.*` — esqueleto de 19 ossos encaixado no crash dummy (o GLB não tem rig utilizável)
- `src/character_model.*` — carrega `crash_dummy.glb` e prende cada peça rígida ao osso mais próximo
- `src/mocap.*` — carrega `Walking.fbx` / `Running.fbx` (Mixamo) e faz o retargeting para o esqueleto do boneco
- `src/animation.*` — andar/correr por captura de movimento; parado, pulo, queda e aterrissagem procedurais; blend entre tudo
- `src/foot_ik.*` — Foot IK: sonda o terreno sob cada pé, ajusta a pelve e dobra os joelhos (IK de 2 ossos)
- `src/table.*` — mesa de madeira (`wooden_table.glb`): posição, colisão (tampo + pernas) e pontos do tampo para colocar armas
- `src/guns.*` — definição de cada arma (pistola Classic, AK-47, AK-12 da pasta `armas/`): empunhadura, guarda-mão, cano, linha de mira, cadência
- `src/loadout.*` — inventário: até 3 armas (pistola na cintura, 2 fuzis cruzados nas costas)
- `src/gun_handling.*` — animações de pegar da mesa, guardar e sacar (cintura e costas), por IK em cima da animação do corpo
- `src/weapon.*` — tiro hitscan pela mira (qualquer arma), clarão, rastro, faíscas, fumaça, cápsulas e marcas de tiro
- `src/aim_ik.*` — pose de mira: IK dos dois braços segurando a arma na linha do olho, mão fechada na empunhadura
- `src/arena.*` — paredes, caixotes de proteção e a porta de dobradiça (abre em tempo real)
- `animation-teste/` — clipes do Mixamo da cobertura (de costas para a parede): `Left/Right Cover Sneak` (de pé, cobertura alta) e `Crouched Sneaking Left/Right` (agachado, cobertura baixa), retargetados como o andar/correr
- `src/ragdoll.*` — ragdoll na morte (jogador e soldados): partículas nas juntas presas por varetas (Verlet), gravidade, colisão com chão e caixas, pose reconstruída das partículas; o tiro que mata empurra o corpo
- `src/cover.*` — cobertura: faces das caixas onde dá para se proteger (baixa até 1,45 m, alta acima), cobertura do jogador e lugares escondidos para os soldados
- `src/soldiers.*` — soldados: esqueleto encaixado no rig do `russian_soldier.glb` (pesos de skin do arquivo), mesmas animações do jogador retargetadas, IA de combate e hitbox por parte do corpo
- `src/physics.*` — cápsula do personagem, degraus, bolas com física, mapa de teste
- `src/renderer.*` + `shaders/` — renderização (ver abaixo)
- `src/graphics_settings.*` — opções gráficas, presets e `settings.ini`
- `src/menu.*` — menu de pausa (Dear ImGui, em `third_party/imgui`)

## Gráficos
Ordem dos passos por quadro:
1. **Céu físico** (`atmosphere.glsl`): espalhamento Rayleigh + Mie + ozônio, calculado numa cubemap quando o sol muda. A cor do sol vem da mesma atmosfera (fica laranja no fim da tarde).
2. **Luz do céu (IBL)**: o céu com nuvens é capturado numa cubemap. Dela saem a irradiância difusa e o reflexo pré-filtrado por rugosidade (GGX).
3. **Sombras em cascata (CSM)**: até 4 cascatas estáveis (presas à grade de texels), PCF com 16 amostras giradas e penumbra de tamanho constante.
4. **Pré-passo** de normais/profundidade → **SSAO** em meia resolução com desfoque que respeita bordas.
5. **Cena HDR com MSAA**: PBR (GGX), sombra das nuvens no chão, materiais procedurais (concreto em placas, blocos pintados com quinas gastas).
6. **Luz volumétrica**: marcha pelo raio da câmera na névoa de altura testando o mapa de sombra (raios de sol). Sem ela, a névoa é analítica.
7. **Bloom** (6 níveis, filtro de Jimenez), **exposição automática** (adaptação do olho), **ACES**, gradação de cor, vinheta, granulação e aberração cromática.
8. **FXAA** e escala para a janela (resolução interna configurável).

As opções ficam no menu (Esc) e são salvas em `settings.ini` ao fechar o menu ou sair do jogo. A hora do dia vai das 5:30 às 19:00; com "Ciclo do dia" ligado, o tempo passa sozinho.

Medição de GPU por passo: `GA_PROFILE=1 ./build/ga` imprime os tempos (ms) a cada 60 quadros.

## Screenshot de uma pose (depuração)
```bash
./build/ga --shot pose.png --anim run --phase 0.25 --cam 90 5 3.4 [--debug]
```
Simular andando para frente (ex.: subir a escada): `./build/ga --shot f.png --pos 6.5 0 3 --sim 2.4 --cam 90 6 3.8` (`--simrun` corre, `--noik` desliga o IK).

Mira/tiro: `./build/ga --shot f.png --aim --pos 0 0 -12 --yaw 180 --cam 0 2 0 --fire 3` (`--gun` só segurando, `--view yaw pitch dist` e `--viewat x y z` para fotografar de outro ângulo).

Pegar/guardar/sacar: `./build/ga --shot f.png --weapon 1 --act pickup 0.55 --view 200 20 2 --viewat 1.6 1 2.3` (`--weapon 0|1|2` = pistola/AK-47/AK-12; `--act stow|draw T` mostra o gesto no tempo T; `--holster` equipa as três no corpo).

Arena: `./build/ga --shot f.png --pos 0 0 33 --npc 3 --view 30 5 7 --viewat 0 1.2 38` (`--npc T` simula os soldados por T s com o jogador em `--pos`, `--door` abre a porta, `--kill N` mata os N primeiros, `--crouch 0.5` agacha o jogador, `--coverclip low|stand 0|1 fase` mostra a pose dos clipes de cobertura).

`--anim`: idle, walk, jog, run, jump, fall, land, bind. `--phase` 0 = pé esquerdo à frente.

Gráficos na captura (usa os padrões, não o `settings.ini`):
- `--hour 18.5` muda a hora do dia.
- `--preset 0..3` escolhe Baixo, Médio, Alto ou Ultra.
- `--gfx nome=valor` muda qualquer opção do `settings.ini` (ex.: `--gfx fogDensity=3`).
- `--menu --bench 3` desenha o menu na foto.
- `--bench 60` mede o tempo por quadro.

`./build/ga --menu` abre o jogo já pausado no menu.
