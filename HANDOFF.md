# Passagem de contexto — porte do Last Chaos para macOS

Leia isto antes de continuar. O plano e o checklist estão em `PORTING_MACOS.md`; aqui está o
**estado do ambiente**, como operar e o que já se aprendeu na prática. Atualizado em 2026-10-19.

## Onde estamos

- Os 6 servidores rodam no Mac (arm64) contra MariaDB local.
- Um cliente **Windows** compilado deste repositório (D3D8, 32 bits, MSVC) loga nesse servidor pela
  rede local, cria e seleciona personagem, entra em Juno (zona 0), anda, luta e usa lojas.
  Ele existe para validar o servidor e conhecer o motor, e é a referência para testar cada passo do porte.
- Cliente **Mac** (2026-10-19, `build-client-mac`, OpenGL 2.1 via SDL3): loga no servidor do Mac,
  entra em Juno, anda, luta, pega itens; trackpad (dois dedos giram a câmera, pinça dá zoom).
  Instalação em `~/Projects/LC-BR2014` (cópia de `C:\LC-BR2014`), aberta por `scripts/macos/play.sh`.
  Shaders (skinning, terreno, efeitos) na GPU e malhas em VBOs desde 2026-10-19 (`369a9d8`).
  Ver "C8 — cliente Mac rodando" e "C8 — shaders na GPU e VBOs" abaixo.
- Branch de trabalho: `macos-port` no fork `git@github.com:PatrickSchifter/LCSource.git` (remote
  `origin`; `upstream` é pwesty/LCSource). Commits terminam com a linha `Co-Authored-By` do Claude.
- O usuário conversa em português; mensagens de commit em inglês.

## Próximos passos sugeridos (em ordem)

Decidido (2026-10-17): renderização em OpenGL primeiro (o backend `GAT_OGL` que o motor já tem,
com skinning na CPU), depois Metal, por último Vulkan; plataforma em **SDL3** (trocado de SDL2 em
2026-10-17: no Homebrew o `sdl2` já é o sdl2-compat rodando sobre o SDL3).

1. Etapa OpenGL concluída (2026-10-19): bloom, reflexo da água, preview da loja, Retina, gama e o
   nome de item com mouse em cima conferidos no Mac. Refração da água não existe no motor (nem no
   D3D: `re_prtRefraction` é declarado e liberado, nunca criado).
2. **Metal** (D2.2), ver "C8 — Metal": M1–M5 feitas; o Metal é o padrão no Mac (`3cfa01a`),
   `LC_GFX_API=opengl` volta ao OpenGL. Juno visto no padrão novo depois da V0 (abaixo).
3. **Vulkan** (D2.3), ver "C8 — Vulkan": V0 feita (`d9459a2`, interface comum `GpuDevice`). Próximo: V1,
   que precisa do MoltenVK. Decidido (2026-10-19): Homebrew (`molten-vk vulkan-loader vulkan-headers
   vulkan-validationlayers glslang vulkan-tools`) e programas ARB → GLSL → SPIR-V pelo glslang em
   tempo de execução. O `brew install` falhou sem rede (ghcr.io inacessível); refazer.

## C8 — Vulkan (2026-10-19)

- Plano: V0 interface comum (feita); V1 dispositivo Vulkan (MoltenVK) até a tela de login; V2 Juno
  sem programas (função fixa emulada em GLSL); V3 programas ARB → GLSL → SPIR-V; V4 render targets e
  leituras da tela; V5 escolha por `LC_GFX_API=vulkan`.
- V0 (`d9459a2`): o que era o backend Metal virou o de toda API sem função fixa. `GpuDevice.h` é uma
  tabela de funções do dispositivo (como os `gfx*` do motor) com tipos C++ simples: estado, streams,
  texturas, render targets, leituras, programas. `MetalDevice.mm` preenche a sua (`mtlDevice()`); as
  structs internas derivam de `GpuTexture`/`GpuBuffer`/`GpuProgram` vazias (sem ODR entre
  dispositivos). Ficam no dispositivo a função fixa emulada (MSL em `MetalDevice.mm`) e a tradução
  do ARB (`TranslateProgram` = `TranslateARBToMSL`); os layouts das constantes (`GpuVPConst`,
  `GpuPPConst`, `GpuFFVertexConst`, `GpuFFFragmentConst`) são comuns e o GLSL tem de seguir os mesmos.
- `Gfx_wrapper_Gpu.cpp` (era `Gfx_wrapper_Metal.cpp`: `gpu_*`, `GPU_*`, `*_GPU`) chama `_pgpu->...`;
  `Gfx_Gpu.cpp` (era `Gfx_Metal.cpp`) tem `InitDriver_GPU(eAPI)`, que escolhe o `_pgpu` da API.
  Nos pontos do motor, `GFX_IsGpuAPI(eAPI)` (`GfxLibrary.h`, hoje só `GAT_MTL`) sob `SE1_GPUDEVICE`
  (definido no CMake do Mac ao lado de `SE1_METAL`). Continuam só do Metal: `HasAPI`, a lista de
  adaptadores (`Adapter.cpp`), a escolha e a volta ao OpenGL no `Nksp.cpp`.
- Para a V1: `GAT_VK` = 3 em `GfxAPIType`, `gl_gaAPI[3]`, entrar em `GFX_IsGpuAPI`, `HasAPI` e
  `InitDriver_GPU`; `VulkanDevice.cpp` preenchendo um `GpuDevice` (sem headers do motor, como o do
  Metal); superfície pelo `SDL_Vulkan_CreateSurface`. Comentários "// Metal" em `Gfx_wrapper.cpp`
  passam a valer para os dois.
- Visto: compila sem erro; jogo no Metal pelo caminho novo, Juno com terreno, skinning, sombra
  projetada, UI e minimapa, nenhum programa falhando no `Nksp.log`.

## C8 — Metal (2026-10-19, M1 em `2c36d7c`)

- Escolha (M5): Metal por padrão quando há dispositivo Metal; `LC_GFX_API=opengl` (ou `gl`) escolhe o
  OpenGL (só na partida, `Nksp.cpp`). Se o Metal não abrir o modo, o `StartNewMode` tenta o OpenGL no
  mesmo modo antes dos modos de recuperação. O `sam_iGfxAPI` persistente não é lido na partida. `GAT_MTL` = 2 existe em
  todas as plataformas (o backend só com `SE1_METAL`, definido no CMake do Mac), então os
  testes de API nas chamadas comuns não precisam de `#ifdef`. `stdout.log` mostra
  `Metal: <GPU>, drawable WxH` quando o Metal abre.
- Camadas: `MetalDevice.h/.mm` é o dispositivo, em Objective-C++ sem headers do motor (o `BOOL`
  do ObjC colide com o do motor); `Gfx_wrapper_Metal.cpp` (incluído pelo `Gfx_wrapper.cpp`) tem
  as `gfx*`; `Gfx_Metal.cpp` inicia/encerra, contexto na janela e fim de quadro. A camada da
  janela é uma `NSView` com `CAMetalLayer` sobre o conteúdo da janela SDL (não pega o mouse);
  a densidade Retina vem de `PLT_GetDrawableScale()` a cada quadro.
- Sem função fixa no Metal: um programa MSL (`_strFixedFunction`) faz o que o OpenGL fazia
  (unidades de textura com MODULATE/2X/REPLACE/INTERPOLATE, matriz de textura, cor por vértice
  ou constante, alpha test 0.5, clip plane em espaço de vista, tamanho de ponto). Os vértices são
  lidos por índice (sem vertex descriptor), um buffer por array, como o motor os tem. O resto do
  estado (blend, profundidade, cull...) vai ao dispositivo a cada draw a partir das `GFX_*`.
- Matrizes no formato do D3D (profundidade 0..1), sem o meio pixel; viewport e scissor do topo à
  esquerda, como o D3D, vezes `GFX_DrawableScale()`.
- Sincronização: o que vem da memória (arrays, índices, constantes) é copiado para um ring buffer
  do quadro (3 quadros no máximo com a GPU). Textura ou buffer alterado depois de desenhado ganha
  armazenamento novo (os command buffers seguram o antigo), então o que foi desenhado antes da
  mudança fica como no OpenGL.
- Texturas: as sem compressão em RGBA8, com os formatos L8/A8/LA8/RGB convertidos como o OpenGL os
  lê; DXT1/3/5 como BC1/2/3 (os assets têm texturas que só sobem comprimidas: sem isso o
  `Read_t` lança "Cannot upload compressed texture").
- `DrawPort`: preencher e z-buffer por `mtlClear` (triângulo no retângulo), pontos/linhas/borda e
  o retângulo de 4 cores pelo `MTL_DrawPrimitives`.
- Programas (M3, 2026-10-19, `165fd29`): o assembly D3D vira ARB pelo mesmo `ShaderAsmToARB.cpp` do OpenGL, e
  o ARB vira MSL em `Graphics/ShaderARBToMSL.cpp` (uma instrução MSL por instrução ARB; o ARB já tem
  a semântica do D3D escrita). Interface comum com a função fixa emulada (`MSL_PROGRAM_INTERFACE`),
  então programa de vértice e função fixa de pixel (ou o contrário) se combinam. As constantes
  (96 de vértice, 16 de pixel: c0..c7 + `ARB_PIXEL_*`) ficam no wrapper e vão a cada draw, com a
  máscara de streams (`mtlGetStreamMask`) para as entradas sem array. c0..c3 e os billboards usam
  as matrizes do Metal (`GetViewAndProjection` no `Shader.cpp`). Alpha test depois do programa,
  como no OpenGL. Clip plane e névoa (`oFog`) ignorados com programa de vértice.
- Visto (M3): Juno com skinning, terreno e efeitos nos programas, 45 programas sem erro, ~11% de
  CPU. `LC_DUMP_PROGRAMS=<dir>` grava o assembly de cada programa criado (`<hash>.vs/.ps`); um
  testador offline (ObjC++: `ShaderAsmToARB.cpp` + `ShaderARBToMSL.cpp` + compilar e ligar no
  `MTLDevice`; não há compilador `metal` offline só com as Command Line Tools) refaz todos sem login.
- Ainda não: consultas de oclusão (o motor usa a leitura de profundidade). O `RenderPatches_View`
  (patches de modelos MDL) chama o OpenGL direto até no D3D; não é usado.
- Visto: tela de login igual à do OpenGL (2026-10-19).
- M2 (2026-10-19, `e3aec26`): Juno no Metal sem programas — campo, cidade (brushes), pontes, árvores com
  alpha test, lobos, pet, efeitos de golpe com partículas e números de dano — igual ao OpenGL no
  mesmo ponto de entrada, menos a sombra projetada do personagem (render target, M4). Faltava só
  `shaPrepareForBillboard` (`Shader.cpp`): saía cedo fora do OpenGL, então os billboards dos
  shaders `Default`/`NormalMap` não giravam para a câmera no Metal; agora lê `GetViewMatrix_MTL`.
  Log sem erros. CPU ainda não comparado com o OpenGL em condições iguais (janela em foco, mesmo lugar).
- Comparar: `scripts/macos/play.sh` (Metal), login sem mexer, captura da janela; depois com
  `LC_GFX_API=opengl`. A câmera guarda o zoom entre sessões, então o enquadramento pode diferir.
- `grep` nos fontes: use `-a` (os bytes coreanos fazem o grep tratá-los como binário e calar).

## C8 — Metal M4: render targets e leituras da tela (2026-10-19, `98defbc`, conferida em jogo)

- `CRenderTexture` (`TextureRender.cpp`) no Metal: textura BGRA8 (o formato do drawable, então os
  pipelines servem para os dois) com profundidade própria (`mtlTextureRenderTarget`).
  `mtlSetRenderTarget` fecha o encoder e o próximo desenho abre um passe no alvo (carrega o que
  ele tinha; quem chama limpa). `Begin()` guarda viewport/scissor do wrapper e põe no alvo inteiro,
  como o `SetRenderTarget` do D3D, e liga `GFX_pixRenderTargetHeight` (escala 1 no alvo); `Clear()`
  limpa com o alfa do D3DCOLOR (a sombra limpa com alfa 0, os previews com 0x00000000).
- O alvo do Metal começa **em cima**, como o do D3D: loja de cash, modelo de item do HUD e reflexo
  da água usam as coordenadas do D3D (`UIManager.cpp`/`UICashShopEX.cpp` só invertem no OpenGL; o
  `-0.5` do reflexo no `Shader.cpp` já era da M3).
- Sombras projetadas (`Render.cpp`): as chamadas GL diretas viraram `GetShadowProjection`/
  `SetShadowProjection`/`SetShadowViewport`/`SetShadowTexGen`, que escolhem a API. No Metal o
  texgen é o do D3D: modo 8 nas coordenadas da função fixa = posição em espaço de vista, pela
  matriz de textura e dividida por q (`MTL_SetTexGenViewPosition`).
- `gfxSetTextureWrapping` no Metal passou a valer também para a textura já posta na unidade
  (antes só na próxima `gfxSetTexture`): a sombra põe CLAMP depois do `SetAsCurrent`.
- Bloom (`RenderBloom.cpp`): o código do OpenGL ficou comum, com `DrawFilterQuad` (GL ou
  `MTL_DrawFilterQuad`, que manda o quad direto ao dispositivo com estado próprio) e a cópia da
  tela por API (`mtlCopyFrameToTexture`, blit na GPU, linhas de cima). `Get/Restore_SRS_Bloom` no
  Metal = `mtlPushState`/`mtlPopState` (streams, texturas, constantes, estado do dispositivo).
- Flares (`DepthCheck.cpp`) e `GrabScreen` (`DrawPort.cpp`): `mtlReadFrameDepths`/`mtlReadFrameRGB`
  copiam do drawable/profundidade para um buffer compartilhado e **esperam a GPU** (o command
  buffer é enviado sem apresentar; o drawable continua no quadro). A profundidade fica de um quadro
  para o outro, então uma leitura antes de desenhar vê o quadro anterior, como no OpenGL. Com
  `gap_iOptimizeDepthReads` = 1 (padrão) é uma espera por quadro quando há flares; se pesar, ler
  assíncrono (resultado do quadro anterior) ou usar visibility queries.
- Testado offline (programa ObjC++ com `MetalDevice.mm` e `MTL_DEBUG_LAYER=1`): clear e desenho no
  alvo, orientação, cópia da tela, leitura de cor e profundidade (dentro e fora), profundidade
  mantida entre quadros — tudo certo, sem avisos da validação. Login no jogo com a validação ligada
  sem erros. **Não visto em jogo** (precisa do login do usuário).
- Conferir em jogo (`scripts/macos/play.sh`, comparando com `LC_GFX_API=opengl`): sombra do
  personagem no terreno e no chão das cidades; loja de cash (preview do personagem) e modelo de
  item no centro da tela; reflexo na água (`g_iReflectionDetail`); bloom (`g_iUseBloom` é
  persistente e pode ter ficado 0 nos testes da M3, quando o `InitBloom` falhava: religar o filtro
  nas opções); flares do sol/luzes (somem atrás de objetos); captura de tela (tecla de print, sai
  na resolução cheia); uso de CPU com flares na tela. `MTL_DEBUG_LAYER=1` mostra erros de API no
  `stdout.log`.

## C8 — handles de textura (2026-10-19, `40f850b`)

- `TEXOBJ` (`Base/Types.h`): objeto de textura da API gráfica; `uintptr_t` fora do Windows (o
  Metal vai guardar ali um `id<MTLTexture>`), `ULONG` no Windows (tipos e código iguais aos de
  antes). Usado em `CTextureData` (`td_ulObject`, `td_pulObjects`, `td_ulProbeObject`),
  `CShadowMap`, névoa/neblina, textura de padrão das linhas, bloom, benchmark e na assinatura de
  `gfxGenerateTexture`/`gfxDeleteTexture`/`gfxSetTexture`/`gfxGetTextureSize`/
  `gfxGetTexturePixRatio`. `GFX_apulCurrentTexture` passou a `TEXOBJ*`. Os nomes das variáveis
  (`ul...`) ficaram, para o diff ser reaplicável nas outras versões (Fase V).
- OpenGL: `glGenTextures`/`glDeleteTextures` passam por um `GLuint` local (antes escreviam 32 bits
  no campo pelo ponteiro); o bind faz `(GLuint)`.
- Continuam `ULONG`: programas de shader (`gfxCreateVertexProgram`...), consultas de oclusão e
  `vb_aulBufferObjects`. Para o Metal, ou tabela no backend com índice de 32 bits, ou o mesmo
  tratamento; decidir quando o backend chegar lá.
- Compila no Mac; não compilado no Windows (SSH recusado no dia), mas lá `TEXOBJ` = `ULONG`.

## C8 — nome de item com mouse em cima; largar item (2026-10-19, `c7e0d31`)

- Nome do item no chão (`ActorMgr::DrawItem`) e o de mouse em cima (`RenderObjectNamePopup`,
  `UIManager.cpp`, caso `ITEM`) saem no mesmo z com teste e escrita de profundidade; `SetOrtho`
  (perto 0, longe -1) com `DepthRange(0,1)` dá o mesmo z no D3D e no OpenGL. Nada a portar;
  conferido em jogo pelo usuário. O "piscar" do código é proposital: nome em modo PK.
- Largar item não funcionava em lugar nenhum (nem no cliente Windows): `do_ItemThrow`
  (`GameServer/doFuncItem.cpp`) saía quando `CanDrop()` era verdadeiro, então todo item largável era
  ignorado em silêncio (a caixa de confirmação fechava e o item ficava). Herdado do upstream.
- No shell do Claude o `grep` é uma função do perfil e deixou de achar arquivos do `Engine` (causa
  não vista): usar `/usr/bin/grep -a`; `sed` com acentos coreanos precisa de `LC_ALL=C`.

## C8 — gama no macOS e acertos do Retina (2026-10-19, `4390866`)

- Gama/brilho/contraste: rampa do monitor pelo CoreGraphics (`CGSetDisplayTransferByTable`,
  `PlatformMac.mm`), aplicada pela camada de plataforma só com a janela em tela cheia e em primeiro
  plano; perde o foco, sai da tela cheia ou fecha → `CGDisplayRestoreColorSyncSettings`. O jogo
  abre os modos sempre "em janela" (`ResetDisplayMode` no `TryToSetDisplayMode` do `Nksp.cpp`) e
  põe a janela em tela cheia, então `GLF_FULLSCREEN` fica desligado: no Mac `GLF_ADJUSTABLEGAMMA`
  vale sempre e quem decide é a janela. Antes o motor zerava brilho/contraste (persistentes) a
  cada quadro. Testado pelo usuário.
- Login: `InitPos` da raiz da UI usava o tamanho pedido (2940x1912) em vez do `_pdpMain` (1470x956
  pontos): fundo esticado e o modal fora da tela (`Nksp.cpp`, `UIOption.cpp`).
- Seleção/criação de personagem (`Contents/Login/BackImageManager.cpp`): tabela de 12 resoluções
  para distâncias; fora dela a distância era 0 (personagem sumia) → entrada mais próxima por
  proporção e tamanho. `GetRatio` tratava qualquer proporção acima de 16:10 como 4:3 (câmera
  perto) → classe mais próxima; nas resoluções da tabela dá o mesmo de antes.
- Notch (MacBook com câmera no topo): para cada largura há o painel inteiro e um modo mais baixo,
  abaixo do notch (1470x956 / 1470x918 pontos). A tela cheia troca pelo mais baixo e a lista
  (`Adapter.cpp`, `PLT_IsBehindNotch`) omite os de painel inteiro; quem tinha 2940x1912 salvo abre
  em 2940x1836. A lista ganhou os tamanhos abaixo do notch; `da_admDisplayModes` passou a 32.
- O contexto OpenGL é atualizado (`PLT_UpdateDrawable`) depois de criado e quando a janela muda.

## C8 — Retina no OpenGL (2026-10-19, `2deb6bb`)

- Decidido com o usuário: a resolução das opções é em **pixels desenhados**; cena nítida com a UI
  no tamanho normal. A lista ganhou 2560x1440..3840x2160 (`Adapter.cpp`), mostrados quando a tela
  tem um modo com esses pixels em densidade > 1; `PLT_GetDesktopSize` dá pixels.
- `PLT_SetMainWindowMode`: tela cheia procura um modo de densidade > 1 com exatamente esses pixels
  (2560x1600 = 1280x800 a 2x), senão o mais próximo em pontos (1x, ampliado como antes); em janela,
  densidade só se o tamanho não cabe no desktop em pontos. A janela fica com pixels/densidade
  pontos e o `NSView` desenha na densidade cheia (`PLT_MacSetHighDensity`, `PlatformMac.mm`; a
  janela é criada sem `SDL_WINDOW_HIGH_PIXEL_DENSITY`, então o SDL segue em pontos).
- O jogo continua contando em **pontos** (tamanhos, mouse, UI, `GetClientRect`); o motor multiplica
  por `GFX_DrawableScale()` (`GfxLibrary.cpp`; 1 dentro de render target) a viewport e o scissor
  (`LockDrawPort`, `DrawPort.cpp`), a cópia da tela do bloom, o `glReadPixels` do teste de
  profundidade dos flares, `glPointSize` e a captura de tela (sai na resolução cheia).
- `StartNewMode` (`Nksp.cpp`) limitava ao desktop - 1 mesmo em tela cheia; agora só em janela.
- `LC_NO_HIGH_DENSITY=1` desliga. Testado em jogo pelo usuário (tela cheia nas resoluções grandes).

## C8 — preview da loja no OpenGL (2026-10-19, `b2f97cf`)

- Drawports travados dentro de um `CRenderTexture` contam as linhas pela altura do alvo
  (`GFX_pixRenderTargetHeight`, posto por `Begin()`/`End()`), não pela da janela; a loja de cash e o
  modelo de item do centro da tela invertem o v no OpenGL (o alvo começa embaixo).
- A loja abre vazia: só pede itens ao clicar numa categoria (`MSG_EX_CASHITEM_LIST_REQ`); o banco não
  tem itens de tipo 1xxxx, então "HOT and New" fica vazia.

## C8 — bloom no OpenGL (2026-10-19)

- `RenderBloom.cpp` compila sem D3D: a matemática (gaussiana, offsets, matriz da cobertura) é
  comum; `InitBloom`, `RenderBloom`, `DoCreateFlareTexture_Separable`, `Get/Restore_SRS_Bloom` e
  `ReleaseBloomTexture` têm versão OpenGL no `#else`. Mesmos passos: tela reduzida a 512x512,
  blur separável de 4 amostras por passe (1º opaco, depois `ONE, ONE_MINUS_SRC_COLOR`), soma na
  tela com `SRC_COLOR, ONE`. Os programas são os do D3D pelo tradutor ARB (no vs, `v7` vira `v5`:
  é onde o OpenGL recebe as coordenadas de textura).
- Diferenças: uma cópia da tela inteira (`glCopyTexSubImage2D`, linhas contadas de baixo) no lugar
  dos tiles de 512 do `CopyRects`; offsets do blur meio texel para trás (o D3D amostra no canto do
  texel, o GL no centro); os passes sem pixel shader do D3D usam o mesmo programa com peso 1 só na
  1ª amostra. O estado do GL é salvo com `glPushAttrib` e devolvido, mais os programas ligados.
- Sem `GLF_VERTEXPROGRAM`/`GLF_PIXELPROGRAM` o `InitBloom` falha e o motor zera `g_iUseBloom`
  (persistente), como no D3D. Testado em jogo pelo usuário (Juno, `g_iUseBloom` 6).

## C8 — render targets e sombras projetadas no OpenGL (2026-10-19, `28e947e`)

- `CRenderTexture` (`TextureRender.cpp`) fora do D3D: FBO (`GL_EXT_framebuffer_object`, funções em
  `Gfx_OpenGL.cpp` como as dos VBOs) com a textura RGBA8 como cor e renderbuffer de profundidade
  de 16 bits. `Begin()` põe viewport e caixa do scissor no alvo (o GL do motor deixa o scissor
  sempre ligado, `DrawPort.cpp` confere) e `End()` devolve; `Clear()` recebe D3DCOLOR (ARGB) e
  limpa ignorando as máscaras, como o D3D. A textura é ligada por `gfxSetTexture()` para o cache
  do motor saber o que está no bind.
- Sombras dos modelos no terreno e nos brushes (`Render.cpp`) saíram do `#ifdef SE1_D3D`; cada
  chamada D3D tem o equivalente GL ao lado (Windows compila o mesmo). A projeção da sombra
  (`D3DXMatrixOrthoRH`) vale como projeção GL (profundidade em 0..1 do -1..1).
  `D3DTSS_TCI_CAMERASPACEPOSITION` + `D3DTTFF_PROJECTED` = texgen `EYE_LINEAR` (planos com modelview
  identidade) + matriz de textura, mesmos floats; mas a textura do D3D começa no topo e a do GL
  embaixo, então `t = q-t` (`SetShadowTexGen_OGL`). Testado em jogo pelo usuário.
- Pendente: render targets não são recriados se o contexto GL for refeito (troca de modo).

## Conversões float→inteiro sem sinal (2026-10-19, concluído)

- Float fora da faixa → tipo sem sinal é indefinido. O MSVC no x86 passa por int com sinal e
  trunca (negativo dá a volta, 256 vira 0). O clang no arm64 varia: no `-O0` satura em 0; no
  `-O3` um `UBYTE(256.0f)` ou `UBYTE(-3.7f)` deixa o valor inteiro no registrador, que quem
  recebe trata como byte (`col | UBYTE(x)` suja os outros canais). Regra: se o valor pode sair
  da faixa, `(UBYTE)(SLONG)(...)`, que dá no Mac o mesmo resultado do x86.
- Corrigidos: giro da câmera (`e53b9eb`), chuva/neve/estrelas (`d32de5a`), mapas de altura do
  servidor e fase do som (`ab577af`), cor das pedras e fade do afterburner (`1774afb`).
- Varredura completa: as conversões float→sem sinal de todo o cliente (motor, entidades geradas
  dos `.es`, GameMP, Nksp; ~300 pontos) saíram do AST do clang e foram revisadas uma a uma. O resto
  é não negativo ou já tem clamp/guarda (cores 0..1×255, tempos, sqrt, `sin*k+offset`).
  Builds de apoio: `build-client-mac-ubsan` (`-fsanitize=float-cast-overflow`) e
  `build-client-mac-asan`.

## C8 — shaders na GPU e VBOs (2026-10-19, `369a9d8`)

- `Graphics/ShaderAsmToARB.cpp` traduz o assembly D3D8 dos shaders (vs.1.1, ps.1.1–1.4) para
  `ARB_vertex_program`/`ARB_fragment_program` na criação do programa: todos os shaders do motor
  (SKA, terreno, efeitos) rodam sem reescrever. Escolhido no lugar de GLSL porque o ARB tem a mesma
  semântica do D3D8 (programas de vértice e pixel independentes, constantes compartilhadas em
  `program.env`). Entradas fixas: `ShaderAsmToARB.h` (pesos/índices = atributos 6/7, tangente = 1).
  Programa que não traduz ou não carrega: uma linha `Cannot translate`/`Cannot load` no console,
  com o código, e a superfície sai sem programa.
- Faltavam no OpenGL (eram só D3D no `Shader.cpp`): c0..c3 (`shaSetVertexProgramConst_TransformMatrix`),
  `shaSetDefaultConstantRegisters` (o terreno saía branco), billboards. A matriz do OpenGL tem o
  mesmo layout de floats da do D3D (`gfxSetViewMatrix` passa os mesmos), então a mesma conta vale.
- Água com reflexo desligada fora do D3D (`Render.cpp`): desenha numa textura, que o OpenGL não tem
  ainda; com shaders ligados aparecia um quadrado azul com o céu.
- VBOs: os buffers estáticos (`gfxCreateVertexBuffer` sem `GFX_DYNAMIC`) ganham um buffer object,
  preenchido no `Unlock` depois de lock de escrita; a cópia em memória fica para as leituras do motor.
  Dinâmicos (morph) seguem em memória. Sem *compiled vertex arrays* quando as posições vêm de VBO.
- **Armadilha achada**: com VBO o ponteiro de um array é um deslocamento, e o deslocamento 0 é
  `NULL`. Uma função que tratava `NULL` como "desligar o array" tirava os índices dos ossos de toda
  superfície que começa no vértice 0 (mãos, pés e espada no osso errado; o tronco escapava porque o
  osso 0 é o dele). Deslocamentos também não podem sair de aritmética em ponteiro nulo.
- Interruptores: `LC_NO_HW_SHADERS=1` (skinning na CPU), `LC_NO_VBO=1` (arrays em memória).
- CPU (Juno, M-series, 60 FPS com vsync): parado ~17%, em combate ~21% de um núcleo; a thread
  principal passa ~80% do quadro esperando o vsync. O que resta é a montagem da cena e o driver.

## C1 — build no Mac (2026-10-18)

```
cmake -S . -B build-client-mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON
cmake --build build-client-mac          # saídas em build-client-mac/Bin
```

- O mesmo `Client Source/CMakeLists.txt` do Windows: terceiros, Ecc, entidades geradas e listas de
  fontes são comuns; D3D8/D3DX, opções do MSVC, `f_socket`, `SharedMemory`, `ShaderCheck` e
  `ZoneMapExport` ficam no ramo `WIN32`, sem mudança. No Mac: SDL3, `vorbisfile` (pkg-config),
  iconv e o framework OpenGL. As bibliotecas saem sem o prefixo `lib` (`Bin/EntitiesMP.dylib`,
  que o motor abre por nome) e se acham por `@loader_path`/`@executable_path`.
- Opções do clang que mantêm a semântica do MSVC: `-fms-extensions -fwrapv -fno-strict-aliasing
  -fno-delete-null-pointer-checks`. Avisos desligados (o check é onde se revisam), **menos
  `-Werror=non-pod-varargs`**, também no check.
- **`CTString` em `...`**: o MSVC passa os bits do objeto (o `char*`); o clang compila a chamada
  como *trap* ("call will abort at runtime") — o primeiro rodar parou em `ParseCommandLine`.
  1.053 argumentos viraram `(const char*)x` (`2f57203`), o que dá no Windows o mesmo valor de
  antes. Código novo: sempre `(const char*)str` em `PrintF`/`CPrintF`/`ThrowF_t`/`FatalError`.
- **Ecc `-line`** (classes do Engine) cortava os blocos no primeiro CR; sem CR (fora do Windows)
  descartava blocos inteiros que começavam com `#line` — sumiu um `for` do `MovableEntity.cpp`, e
  o que sumia dependia do tamanho do caminho do build. Agora corta no LF (`6e4ab40`).
- Funções `inline` definidas num só `.cpp` (`_SFunc`, `GetVertices`, `RMRender.cpp`...) e globais
  definidos no `Engine.h` (o `dllimport` os tornava declarações no Windows): no Mac são símbolos
  indefinidos (só no Release) ou duplicados. Corrigidos; o padrão pode reaparecer em código novo.
- zlib 1.2.8 e libpng 1.6.3 confundem `TARGET_OS_MAC` com o Mac clássico: flags no CMake
  (`Z_HAVE_UNISTD_H`, `fdopen=fdopen`, `-include math.h`, `PNG_ARM_NEON_OPT=0`), sem mexer neles.
- Contexto OpenGL (`6a551d8`): janela principal com `SDL_WINDOW_OPENGL`; `InitDriver_OGL` carrega
  as funções por `SDL_GL_GetProcAddress`; `CreateContext_OGL(HWND)` tenta as mesmas combinações de
  profundidade/stencil do Windows; swap e vsync pelo SDL; sem gama (o SDL3 não tem). Perfil legado
  (OpenGL 2.1 no macOS). Janelas-filhas e *work canvas* das ferramentas: só Windows.
- Rodar: `scripts/macos/play.sh` copia `build-client-mac/Bin` para `~/Projects/LC-BR2014/Bin` (cópia
  da instalação do Windows sem `Bin`, `ServerMaps` e capturas; `LC_GAME_DIR` troca o lugar) e inicia
  com o argumento do `jogar.bat` e `LC_NO_HW_SHADERS=1`; saída em `stdout.log` na instalação.
  O diretório do jogo é o pai de `Bin/`. Um `MessageBox` (modal do SDL) não sai com SIGTERM:
  `kill -9`. Pilha de um processo parado: `sample <pid> 1 -file x.txt`; crash: `lldb`.
- O build do Windows compilou sem erros com tudo isso (`1bcb824`) e está instalado em
  `C:\LC-BR2014\Bin`. Testado em jogo pelo usuário em 2026-10-18, sem problemas:
  login e mundo (as mensagens de chat/sistema com nomes, `_S()`), tooltips de itens, console
  (`CPrintF` com nomes de arquivos), carga de mundos com terreno (`TerrainMapCrop`), modelos SKA
  com animações (`RMRender.cpp`), barra de progresso.

## C8 — cliente Mac rodando (2026-10-18/19)

`scripts/macos/play.sh` (com `LC_CONSOLE_LOG=1` grava `Nksp.log` na instalação; `LC_DEBUG_INPUT=1`
mostra os eventos do trackpad no `stdout.log`; um `ThrowLog.txt` vazio na instalação registra toda
exceção do motor). O login é digitado pelo usuário (o auto-login está desligado para o Brasil).
Ver a janela do jogo sem capturar a tela do usuário: o ID da janela pelo `CGWindowListCopyWindowInfo`
e `screencapture -l <id>`. Crash: `~/Library/Logs/DiagnosticReports/Nksp-*.ips` (JSON, com arquivo
e linha). Pilha de um processo vivo: `sample <pid> N -file x.txt`. Pixels do GL: lldb com
breakpoint em `CGfxLibrary::SwapBuffers` e `pglReadPixels`.

O que o Mac exigiu (todos commitados, `dd78396..fb5c009`):
- Arquivos: o tinyxml abria os XML da UI com o `fopen` dele (sem `\`→`/`); CRC das classes de
  entidade pelo `.dylib` (o mundo pede `Bin\EntitiesMP.dll`), calculado uma vez por biblioteca
  (calcular a cada entidade deixava a carga de Juno acima do timeout de 70 s do servidor).
- Diferenças de plataforma: `strupr` em literal (somente leitura no Mac) em `CTag::SetName`;
  `#ifdef BIG_ENDIAN` sempre verdadeiro no macOS (`<machine/endian.h>`), que deixava os valores de
  64 bits da rede sem troca de bytes; `%I64d` traduzido para `%lld` e o "%s com número" tolerado
  também no arm64 (`CTString::VPrintF`, slots de 8 bytes); `vsnprintf` cortava textos > 255;
  `(UWORD)(float negativo)` = 0 no ARM (`SetRotation`/`GetRotationDiff` do `Player.es`).
- OpenGL: modos Retina na tela cheia (o SDL escolhia 1920x1200 em 1x); `GL_ARB_texture_env_combine`
  aceito para a multitextura (sem ela o terreno caía); clamp to edge pela versão 1.2+ (sem ele a luz
  dinâmica dos golpes escurecia um quadrado de terreno); a mistura dos mapas de sombra do terreno em
  `GL_COMBINE`/`GL_INTERPOLATE` (sem ela o terreno saía escuro).
- Janela e entrada: o SDL não minimiza mais ao perder o foco (minimizado o jogo não desenha);
  trackpad por monitores do AppKit (`Platform/PlatformMac.mm`): dois dedos fora da UI giram, sobre a
  UI rolam; a decisão é tomada no início do gesto; a pinça manda passos inteiros de roda.
- Dados: o `fopen` do macOS abre pastas (só a leitura falha); `LC_fopen` as recusa, como o Windows.
  Itens que o servidor tem e os dados BR não (vêm como `CItemData` vazio) aparecem como saco de
  tesouro em vez de `FatalError` ao carregar o modelo `''` (como os NPCs sem modelo, que são omitidos).
- Desempenho: vsync sempre ligado (o jogo não tem limite: 170 FPS numa tela de 60 Hz); névoa enviada
  à GPU só quando muda (o reenvio a cada quadro esperava a GPU); escondido ou minimizado o loop roda
  a `sam_iMaxFPSInactive` (o `LimitFrameRate()` nunca era chamado). Resultado: ~20% de um núcleo em jogo.

## C5 — andamento (2026-10-17)

Feitos e commitados (um commit por item, Engine separado de Nksp/jogo): itens **1–7**
(`6842344`..`24271f5`, mais `4b6f076`), **8** (`0e1689a`, `a75341f`, `55dfacd`), **9**
(`ee9d8f1`, `2a1a209`, `64dce5e`), **10** (`e7c707a`) e **11** (`46ed8a1`,
`c755cd9`). Check do Mac (com o Nksp no conjunto `ALL`): 12.065 → 6.998 erros únicos; o que
sobra é D3D/D3DX (C8), som (C7), rede/web/IPC do launcher (C6), conformidade C++ (C1) e o
`exploration3d` (ferramentas).
Testado em jogo no Windows: itens 1–7 com o cliente Win32 (Juno, combate, loja, chat, print) e,
no **modo SDL** (`LC_PLATFORM_SDL=ON`), janela, minimizar/maximizar, digitação e atalhos (8c).
Os itens 9–11 foram testados em jogo no modo SDL em 2026-10-18 (lista em "C5 — testes" abaixo) e,
no mesmo dia, o cliente Win32 de novo com `LC_PLATFORM_SDL=OFF`. **C5 concluída.**
O build do Windows (`C:\LCSource-build`) voltou para `LC_PLATFORM_SDL=OFF`, e é essa versão que
está instalada em `C:\LC-BR2014\Bin`.
No Mac, a camada SDL (janela, eventos → `MSG`, cursores `.cur`) foi exercitada num programa de
teste com o SDL3 do Homebrew.

Depois da C5: restante da C1 (CMake do cliente no Mac, link) e as fases C6/C7/C8. Da C1, os
parsers do Engine já são gerados no build (testado no Windows até a tela de login), os includes
usam a caixa do disco, e a região (`LC_CLIENT_COUNTRY`) e o `time_t` de 64 bits estão resolvidos
(2026-10-18, testados em jogo no Windows: datas e prazos, radar, teleporte); falta o CMake de
verdade no Mac. O que ficou
para elas: joystick pelo SDL (hoje sem joystick fora do Windows), Retina/HiDPI e o contexto
OpenGL (C8), `Web.cpp`/IPC/`ThreadWrapper` (C6). O link no Mac precisa de `-liconv` e SDL3, e
os cursores (`Nksp/res/cur_*.cur`) em `Bin/Cursors/`.

### C5 — testes (modo SDL no Windows; todos feitos em 2026-10-18)

1. Cursores do jogo: normal, ataque (sobre monstro), conversa (sobre NPC), pegar item.
2. Câmera e movimento: girar com o botão direito segurado, zoom com a roda, clique para andar.
3. Teclas seguradas: Ctrl/Alt/Shift + clique, Alt+1/2/3 na barra rápida, Tab para alvo.
4. Copiar e colar no chat (Ctrl+C / Ctrl+V), incluindo texto com acentos vindo de fora do jogo.
5. Clique duplo (equipar item do inventário), roda do mouse em listas (loja, chat).
6. Janela: perder e ganhar foco (Alt+Tab), minimizar e voltar; tela cheia nas opções de vídeo.
7. Fechar pelo X da janela e pelo menu do jogo.
8. Depois, voltar o build para `LC_PLATFORM_SDL=OFF` e conferir o cliente Win32 de novo.

Copiar e colar nas caixas de texto só existe em builds `VER_TEST` (`m_bCopyAndPaste`,
`UIEditBox.cpp`), como no cliente original: no nosso build de release o Ctrl+V não cola e o
`WM_CHAR` 0x16 entra no chat (a fonte o desenha como "A"). Não é regressão; ficou como está.

Decisões e lições:
- `Engine/Base/Win32Compat.h` (+ `.cpp`), só fora do Windows: tipos, mensagens e funções
  autocontidas do Win32/CRT (tempo, timers multimídia, memória, arquivos, INI, `LoadLibrary`,
  codepages). O que mexe em janela/cursor está em `Engine/Platform/Win32WindowSDL.cpp`.
- **Caminhos:** dentro do motor tudo continua com `\`; no Mac o caminho absoluto começa com uma `\`
  (`\Users\...\LastChaos\`). A conversão para `/`, com resolução sem diferenciar maiúsculas, é feita
  só na fronteira (`fopen` é um macro para `LC_fopen`; `std::fstream`, `remove` e `rename` usam
  `HOSTPATH()`). `GetModuleFileName(NULL)` devolve o executável nesse formato, então o diretório do
  jogo continua sendo o pai de `Bin\` (no Mac: `<jogo>/Bin/Nksp`). Testado num volume APFS
  case-sensitive. Gravação continua no diretório do jogo, como no Windows (separar fica para a Fase F).
- **Streams:** o Windows carrega o arquivo página a página por falta de página (VirtualAlloc + SEH).
  Fora dele o buffer é `mmap` anônimo, o arquivo é lido inteiro na abertura (`FileLoadBuffer_t`) e o
  fechamento grava até a posição máxima. Custo: `FileExists()` abre o arquivo e agora lê tudo (16
  chamadas, quase sempre seguidas da carga do mesmo arquivo).
- `.gro`: `pread` síncrono (o caminho "9X" do `Unzip.cpp`). Registry: não existe fora do Windows
  (só o importador das ferramentas e a velocidade da CPU o usavam). `GetACP()` = codepage do país
  (BR 1252, KOR 949...); `iconv` com `UTF-32LE` (o `WCHAR_T` do iconv do macOS depende do locale).
  **O link no Mac vai precisar de `-liconv`.**
- DLLs do motor: `LoadLibrary("...\\X.dll")` abre `X.dylib` no mesmo lugar (`dlopen`).
- Só no Windows (como o GameGuard na C4): varredura de processos à procura de bots AutoIt, IPC e
  autoatualização do launcher, versão lida do recurso do executável, instância única, trava do
  diretório, recarga de mod, multimonitor do Win9x, gancho de teclado, pretouch de memória, SEH.
- SDL3: eventos viram `MSG` (teclas com `VK_*` do Windows; `WM_CHAR` para Enter/Tab/Backspace/Esc
  e Ctrl+letra, texto digitado convertido para o codepage do cliente; clique duplo a cada 2º clique;
  roda em unidades de 120 com coordenadas de tela). Por ora 1 ponto da janela = 1 pixel (Retina é
  da C8); a tela cheia usa o modo de vídeo mais próximo pelo SDL. Cursores do jogo (recursos `.cur`)
  ainda não existem no modo SDL (item 9); `MessageBox` no Mac é do item 11.

**Testar o modo SDL no Windows** (o mesmo código que o Mac usa para janela e eventos, com o D3D8
desenhando na janela do SDL):
```
scripts/windows/wps.sh <<'EOF'
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"" >nul && ""C:\Program Files\CMake\bin\cmake.exe"" -DLC_PLATFORM_SDL=ON -DSDL3_DIR=C:/LCSource-deps/SDL3-3.4.18/cmake C:\LCSource-build"
EOF
LC_NO_SYNC=1 scripts/windows/build.sh && scripts/windows/deploy.sh
```
Depois voltar com `-DLC_PLATFORM_SDL=OFF` (cada troca recompila tudo, ~15 min). O SDL3 3.4.18
(pacote VC, sha256 conferido) está em `C:\LCSource-deps\SDL3-3.4.18`; no Mac, `brew install sdl3`.

## C5 — roteiro (levantado em 2026-10-17)

Linha de base do check (`build-client-check-all`, `-DLC_CHECK_DIRS=ALL`): 12.065 erros únicos
contados por `arquivo:linha: mensagem` (`grep error: | sed 's/^.*Client Source\///' | sort -u`).
Os mais comuns: `UINT16` (783), `MSG` (575), `GetProcAddress` (463), `HRESULT`, `MK_*`, `WM_*`, `BYTE`.

**Estratégia.**
- Windows continua igual: o código Win32 de hoje vira o ramo `_WIN32`; fora dele, POSIX + SDL3.
  Onde o C++ padrão faz o mesmo (`Sleep`, `stricmp`...), um único caminho para os dois.
- **A UI continua falando `MSG`.** `CUIManager::MsgProc(MSG*)` recebe as mensagens do loop do
  `Nksp.cpp`; 346 arquivos usam `MSG` e 131 testam `WM_*`/`VK_*`/`MK_*`. Fora do Windows,
  `Engine/Base/Win32Compat.h` define esses tipos e constantes **com os valores do Windows**, e a
  camada SDL3 traduz cada evento num `MSG`. Reescrever a UI fica fora de escopo.
- **Testar a camada SDL3 no Windows:** opção `LC_PLATFORM_SDL` no CMake do cliente Windows, que
  troca WinMain/WndProc/hooks pelo loop SDL3 e passa ao D3D8 o HWND da janela SDL
  (`SDL_PROP_WINDOW_WIN32_HWND_POINTER`). Assim janela, input e IME do Mac são exercitados no jogo
  real antes de o cliente do Mac linkar. Com a opção desligada o Windows fica como hoje.

**Ordem** (um commit por item; os itens 1–7 mexem em código comum e são testados no Windows):

1. **Tipos e constantes** — `Engine/Base/Win32Compat.h`, incluído por `Base/Types.h` em
   `PLATFORM_UNIX`: `BYTE`, `WORD`, `UINT8/16`, `HANDLE`, `HRESULT`, `WPARAM`/`LPARAM`, `MSG`,
   `SYSTEMTIME`; `WM_*`, `VK_*`, `MK_*`, `MB_*`, `CP_*`; `LOWORD`, `ZeroMemory`; nomes do CRT
   (`stricmp`/`strcmpi`/`strnicmp`, `_strupr`, `_atoi64`, `_snprintf`, `_itoa`). Nos `StdH.h`
   (Engine, EntitiesMP, GameMP, Shaders, Nksp), `windows.h`/`winsock2.h`/`mmsystem.h`/`conio.h`/
   `crtdbg.h`/`malloc.h` só em `_WIN32`.
2. **MSVC-ismos** — `#if _MSC_VER >= 1600` / `<= 1200` (`Quest.cpp/.h`, `SingletonBase.h`,
   `Engine.cpp`, `EntityClass.cpp`, `Shader.cpp`, `ItemData.h`, `MobData.h`); `__try/__except`
   (`Engine.cpp`, `RendASER.cpp`); `_control87` (`Math/Float.cpp`, `Nksp.cpp`: no ARM não há
   precisão de 24 bits, vira no-op documentado); `_msize` (`Memory.cpp`, `MemoryTracking.cpp`:
   `malloc_size`); `OutputDebugString` (9 arquivos) → stderr.
3. **Tempo e threads** — `Base/Timer.cpp` (`QueryPerformanceCounter`, `timeSetEvent` → thread com
   `std::chrono`); `timeGetTime`/`GetTickCount` (21 arquivos, quase todos UI) e `Sleep` (17) com
   implementação compat; `Base/Synchronization.cpp` (`CRITICAL_SECTION`) → `std::recursive_mutex`;
   `Network/ThreadWrapper.cpp`, `ProgressHook.cpp`, `Unzip.cpp`, `include/F_Critical.h`,
   `GameMP/Game.cpp`; `GetLocalTime`/`SYSTEMTIME` (7 arquivos) → `localtime_r`.
4. **Arquivos** — ponto único: `ExpandFilePath` (`Base/Stream.cpp`). Fora do Windows, `\` → `/` e
   resolução sem diferenciar maiúsculas (cache por diretório; os assets têm caixa misturada e não
   vamos depender do APFS). `_findfirst` (`Stream.cpp`, `Directory.cpp`, `Entities/SmcParser.cpp`),
   `CreateDirectory`, `_open`. Os 55 arquivos que chamam `fopen` direto com caminhos montados à mão
   passam por um `FOpenExpanded`/equivalente. Diretório do jogo (`_fnmApplicationPath`, hoje via
   `GetModuleFileName`) e escrita (logs, screenshots, preferências) em
   `~/Library/Application Support/LastChaos`.
5. **Configuração** — `GetPrivateProfileString`/`WritePrivateProfileString` (6 arquivos, 37
   chamadas) com um leitor INI próprio, mesma semântica; `Base/Registry.cpp` e
   `Graphics/MultiMonitor.cpp` → INI no diretório do usuário.
6. **Bibliotecas dinâmicas** — `LoadLibrary`/`GetProcAddress` de EntitiesMP, GameMP e Shaders
   (`Entities/EntityClass.cpp`, `Nksp.cpp`, `Graphics/Shader.cpp`) → `dlopen`/`dlsym` com `.dylib`
   (o link estático da C1 pode vir depois). Os de sistema (`user32`, `ole32`, `dbghelp`, `dsound`,
   `vorbisfile`, `amp11lib`, `cwebpage`, `opengl32`) ficam com C6/C7/C8 ou viram no-op.
7. **Texto** — `MultiByteToWideChar`/`WideCharToMultiByte`/`IsDBCSLeadByte`/`GetACP`
   (`Util.h`, `Help/Util_Help.cpp`, `UIListBox`, `UIFactory`, `UITextBox(Ex)`) com `iconv` e o
   codepage do país (BR = cp1252, KOR = cp949...).
8. **Janela e loop (SDL3)** — `Nksp.cpp` (`WinMain`, `SubMain`, loop de `PeekMessage` em ~1279),
   `MainWindow.cpp`, `SplashScreen.cpp`: init SDL3, janela (`_hwndMain` passa a guardar a janela),
   eventos → `MSG` (mouse, teclas com `VK_*` a partir dos scancodes, `WM_CHAR`, foco/ativação,
   quit, resize), `IsIconic`/`ShowWindow`. Modos de vídeo e monitores (`MultiMonitor.cpp`,
   `ChangeDisplaySettings`/`EnumDisplaySettings` em `GfxLibrary.cpp`, `Adapter.cpp`,
   `GetSystemMetrics`) → displays do SDL. `CreateWindowCanvas` (`ViewPort.cpp`) recebe a janela SDL;
   o contexto OpenGL em si é da C8.
9. **Input** — `Base/Input.cpp`: `GetAsyncKeyState` → estado do teclado/mouse do SDL; hooks
   (`SetWindowsHookEx`) → eventos vindos do loop; `ClipCursor`/`SetCursorPos`/`GetCursorPos` →
   mouse grab, warp e modo relativo; joystick (`joyGetPosEx`) → SDL joystick/gamepad.
10. **IME e entrada de texto** — `Interface/UIIME.cpp` e `UIEditBox.cpp` (`Imm*`, composição,
    candidatos) → `SDL_StartTextInput`, `SDL_EVENT_TEXT_EDITING`/`TEXT_INPUT`,
    `SDL_SetTextInputArea`, encaixados no fluxo de composição que a UI já tem.
11. **Resto** — `MessageBox` (`ErrorReporting.cpp`, `TradeItem.cpp`) →
    `SDL_ShowSimpleMessageBox`; `ShellExecute` (6 arquivos) → `SDL_OpenURL`; área de transferência
    (`UIEditBox.cpp`) → SDL; `StackDump.cpp` fora do Windows: handler de sinal com `backtrace()`;
    tirar `odbc32`/`odbccp32` do link (nenhuma chamada `SQL*` no cliente).

Fora da C5: Winsock/wininet/`Web.cpp`/`F_Socket` (C6), DirectSound/waveOut (C7), D3D8/D3DX e o
contexto/funções OpenGL (C8), `exploration3d`/`Object3D_IO.cpp` (importador de modelos das
ferramentas).

## C6 e C7 — como ficou (2026-10-18)

Feitas em paralelo por dois agentes, cada um num worktree, e juntadas no `macos-port`
(C7 `3b366e5`..`fd81b47`, C6 `eacb64c`..`ef6097c`). Check do Mac (`ALL`): 864 → 128 erros únicos,
0 `fatal error`; sobram OpenGL (`Gfx_OpenGL.cpp` 71, `GfxLibrary.cpp` 24, `ViewPort.cpp` 21),
`exploration3d`/`Object3D_IO` (ferramentas) e dois `Min` em `RenderModels.cpp` ~1612.
Windows sem as opções novas: o código resolvido (`unifdef`) é o mesmo de antes, salvo grafias.
**Nada disso rodou em jogo ainda.**

**C7 (som).**
- Macro `LC_SOUND_SDL` (`Engine/Base/Base.h`, ligada em todo compilador não-MSVC). No Windows é a
  opção CMake `LC_SOUND_SDL` (padrão OFF, exige `LC_PLATFORM_SDL=ON`): dá para testar janela SDL com
  DirectSound ou com som SDL.
- Modelo de empurrar: `UpdateSounds()` chama `MixSounds()` até 20×/s; `PrepareSoundBuffer_sdl()`
  calcula quanto falta para a fila do stream ter `snd_tmMixAhead` segundos, o mixer mistura isso em
  `sl_pswMixerBuffer` e `CopyMixerBuffer_sdl()` entrega com `SDL_PutAudioStreamData`. Stream S16
  estéreo, 11/22/44 kHz conforme `snd_iFormat`; `Mute()`/`Flush()` = `SDL_ClearAudioStream`. Se o
  jogo travar mais que o mix-ahead sai silêncio (o DirectSound repetiria o buffer).
- Só o mixer por software (= `snd_iInterface=1`, o padrão no Windows). `snd_iDevice` = índice na
  lista do SDL (-1 = padrão). Áudio iniciado com `SDL_InitSubSystem` em `CSoundLibrary::Init`.
- Ogg: libvorbisfile linkado direto (pkg-config) fora do Windows; MP3: dr_mp3 compilado dentro de
  `SoundDecoder.cpp` (guarda o pedaço de quadro que sobra, porque o amp11lib devolvia qualquer
  número de bytes). Não se sabe se os assets têm MP3: conferir `*.mp3` nos `.gro`/`amp11lib.dll` no `Bin`.
- **O link no Mac precisa de `vorbisfile` (pkg-config) e SDL3.** `libvorbis` está no `scripts/macos-deps.sh`.

**C6 (rede).**
- `Engine/Network/SocketCompat.h`: fora do Windows, os nomes do Winsock sobre sockets BSD
  (`SOCKET`, `closesocket`, `WSAGetLastError` = `errno`, `WSAE*` = valores de `errno`,
  `ioctlsocket(FIONBIO)` via `fcntl`); `WSAStartup` ignora `SIGPIPE` (senão um `send()` numa conexão
  fechada mata o processo). No Windows só acrescenta `typedef int socklen_t`.
- `TcpIpConnection.cpp`: `connect` não bloqueante aceita `EINPROGRESS` e a falha vem por
  `getsockopt(SO_ERROR)` (no Windows aparece no except set); `select` com descritor + 1.
- `Compression.cpp`: `uLongf` do zlib tem 64 bits no Mac (o cast antigo gravava 8 bytes num `SLONG`).
- `NetworkMessage.h` tira a macro `R_OK` do `<unistd.h>` (colidia com `CNetworkStream::R_OK`).
- Bug antigo mantido só no Windows: `SO_KEEPALIVE` com nível `IPPROTO_TCP` em
  `TcpIpConnection::InitConnection` (no Windows não liga nada; no macOS o número é `TCP_NOOPT`).
- Só no Windows: `Web.cpp` (wininet + `cwebpage.dll`: quadro de mensagens web, notícias, ajuda e
  loja de cash nos sites do publisher), `SharedWebData.cpp`, `ThreadWrapper.cpp`. Fora do Windows
  `Request()` descarta o pedido, `Read()` nunca devolve página e `OpenWebPage()` devolve FALSE.
  Se um dia precisar: libcurl e uma web view.
- `F_Socket` é código morto (pode ser apagado como na C4: lib `f_socket` e `include/F_*.h`).
- Para o link: `#pragma comment(lib, ...)` (`ws2_32`, `imm32`, `winmm`) não estão atrás de `#ifdef`.

**Armadilhas de edição.** Muitos fontes estão em CP949 com CRLF: `grep -I` os pula como binários
(use `grep -a`), e o Write/Edit apaga espaços no fim das linhas. Edite com substituições exatas em
latin-1 (`perl -pi` ou Python) e confira o `git diff`.

### C6 e C7 — testes no Windows

Build padrão (regressão): **itens 1, 2 e 4 feitos em 2026-10-18**, sem diferença (login, troca de
servidor, LoginServer desligado e GameServer derrubado com mensagem e sem travar, som). O item 3
ficou de fora por decisão do usuário (as páginas web dependem dos sites do publisher).
1. Login, lista de servidores, escolher canal e entrar no mundo; voltar para a seleção de servidor e
   entrar de novo; trocar de canal.
2. LoginServer desligado: falha com mensagem, sem travar (3 tentativas de 3 s). Servidor derrubado
   durante o jogo: mensagem de desconexão.
3. Janelas web (notícias, ajuda, loja de cash, quadro de mensagens) como antes; sair do jogo normalmente.
4. Som ambiente, combate, música e volumes iguais a antes.

Com `-DLC_PLATFORM_SDL=ON -DLC_SOUND_SDL=ON` (depois voltar para OFF): **feito em 2026-10-18**, sons
normais (com a macro o DirectSound nem é compilado, então o som ouvido é o do SDL3). Esse build
não grava `Console.log` em `C:\LC-BR2014`; o item 8 não foi conferido pelo log.
5. Música Ogg: começa, faz loop, troca ao mudar de zona.
6. Sons 3D e de UI; volumes de som e música; latência.
7. Tela de loading/troca de mundo, minimizar e Alt+Tab: sem estalo nem repetição.
8. `snd_iFormat`/`snd_tmMixAhead` pelo console; `Console.log` mostra "SDL audio initialization".

## C4 — como ficou (2026-10-17)

Commits `a325b3a`..`5ff5a2c`, um por item. Quase tudo era código morto (`KALYDO`, `USE_GAMEGUARD`,
`COPY_PROTECTION` e o XTrap nunca estavam ligados); a regra foi **apagar mantendo o ramo que já
compilava**, para o Windows continuar igual. Ferramenta útil: `unifdef` (vem no macOS), por exemplo
`unifdef -m -UKALYDO arquivo` (não simplifica condições compostas, como `!defined(KALYDO) && ...`;
essas foram editadas à mão). Em `Define_RUS.h` ele acusa "EOF in comment" e não grava nada.

- Removidos: `Engine/XBox`, GameGuard, XTrap (e `LC_NO_XTRAP`/`NO_GAMEGUARD` do CMake), Kalydo
  (`Engine/Kalydo`, `Kalydo/`, `Nksp/Social`, `.kcproj`...), `Protection.cpp` (Blowfish/TSE.key),
  `MiniDump.*` (estava atrás de `USE_MINIDUMP`, nunca definido), Visual Leak Detector, dxtlib, Intel IJL,
  todo o `Lib/` pré-compilado, `CDCheck.h`, a cadeia `FGC_Net*`/`mail.cpp` (fora do build) e
  `Engine/Def_make`. `MSG_EX_NPROTECT` e `MSG_EX_XTRAP` ficam no enum (numeração do protocolo).
- `IFeel.cpp`: stubs; `IFeel_InitDevice` ainda declara os símbolos do shell (`inp_fIFeelGain` é
  persistente) e devolve FALSE, como antes sem o `ImmWrapper.dll`.
- `Base/StackDump.cpp` (o `ErrorLog.txt`): compilado só com `_WIN32`; fora dele, stubs da API até a C5
  trazer um handler de sinal com `backtrace()`.
- Screenshot JPEG: `stb_image_write.h` vendorizado em `3rdparty/stb/` (commit fixado no README de lá).
  O arquivo continua terminando com o trailer de 4 bytes (`~tamanho do JPEG`).
- `scripts/vcxproj-to-cmake.py` ignora arquivos que não existem mais: apagar um `.cpp` e regenerar
  as listas basta (o gerador continua sendo a fonte de `cmake/*_sources.cmake`).
- Check do Mac: 0 `fatal error`, nenhum erro novo, 3.453 → 3.306 erros únicos (os que sumiram eram
  Win32). Windows: 0 erros; login, entrada em Juno e screenshot JPEG (orientação, cores, trailer) conferidos.
- Fica para outras fases: D3D8/D3DX (C8), DirectSound/winmm (C7), Winsock/wininet/F_Socket/SharedMemory
  (C6), imm32/registry/janela (C5), ODBC (`odbc32` linkado no Engine/Nksp: ver se algo usa, C5).
- Achado para a C1/C5: vários `#if (_MSC_VER >= 1600)` (`Quest.cpp/.h`, `SingletonBase.h`, `Engine.cpp`,
  `EntityClass.cpp`, `Shader.cpp`) e `_MSC_VER <= 1200` (`ItemData.h`, `MobData.h`) escolhem no clang
  o ramo do VC6/VS2008, não o que o Windows compila hoje. Trocar por `#if !defined(_MSC_VER) || ...`
  conforme o caso.

## Assembly x86 (C3) — como ficou

- `Engine/Base/Base.h` define `USE_PORTABLE_C` em todo compilador que não seja MSVC x86. Com ele,
  cada bloco `__asm` tem um caminho C (`#ifdef USE_PORTABLE_C`, ou `ASMOPT 0` nos arquivos que já
  usavam `#if ASMOPT == 1`). No Windows o asm continua sendo o padrão e é a **referência**.
- O C reproduz o asm, não a intenção: mesma aritmética de ponto fixo, saturação/wraparound do MMX,
  `fistp` arredondando para o mais próximo (e `0x80000000` fora da faixa, em `FloatToInt`), ordem de
  leitura/escrita em filtros in-place, e as esquisitices do original (comentadas no código: alpha
  2× no sombreamento de modelos, alpha 255→254 na cor difusa, eixos trocados na distorção da água,
  sinal de `DDL2oDV` somado em `DDL2oDUoDV` no LayerMixer...). Os ramos C antigos da Croteam
  (ASMOPT) estavam quebrados ou com outra fórmula e foram corrigidos.
- Precisão: o cliente D3D roda a FPU x87 em 24 bits (device criado sem `D3DCREATE_FPU_PRESERVE`),
  então o C usa produtos em `float` onde o asm faz contas no x87. `SetFPUPrecision`/`_control87`
  (`Math/Float.cpp`) não existe no ARM: fica para a C5.
- Sem C (código morto): dithering de `DitherBitmap` (um `goto` desliga), swizzle D3D (Xbox,
  `bSwizzle` sempre FALSE). `StackDump.cpp` sem stack walk e `DetectCPU` sem CPUID fora do x86;
  `OBFUSCATE()` e os anti-debug viram nada; `ReadTSC` usa `steady_clock` em ns.
- **Teste no Windows com os caminhos C:** `-DLC_USE_PORTABLE_C=ON` no configure (opção do CMake do
  cliente). O que exercitar: mundo (lightmaps: LayerMixer, FilterBitmap), modelos animados com
  luz/névoa (RenderModel_View, Shader), água/fogo animados (TextureEffects), texturas (mipmaps,
  conversão de cor D3D), UI (cores), som ambiente e de combate (SoundMixer). Depois voltar para OFF.
  Testado em 2026-10-17 (login, Juno, combate, som): sem diferença visível em relação ao asm. O
  build do Windows (`C:\LCSource-build`) voltou para OFF.
- Achado à parte: `Shader.cpp` ~1461 liga referência não-const a temporário (erro no clang, C1).
  (O `ScanF("%I64x")` de `Stream.cpp` saiu junto com a proteção contra cópia, na C4.)

## Verificação do cliente no Mac (`LC_CLIENT_CHECK_ONLY`)

```
cmake -S . -B build-client-check -G Ninja -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON -DLC_CLIENT_CHECK_ONLY=ON [-DLC_CHECK_DIRS=ALL]
cmake --build build-client-check -- -k 0 2>&1 | tee check.log
```

Desde 2026-10-18 o check (`ALL`) dá **0 erros** e o cliente de verdade compila no Mac ("C1 —
build no Mac"); o check continua sendo o lugar dos avisos de 64 bits.

- **Região:** uma só opção, `LC_CLIENT_COUNTRY` (padrão `BRAZIL`), definida em
  `Client Source/CMakeLists.txt` antes do desvio para o check; Windows, check e o futuro build do
  Mac usam a mesma lista `LC_CLIENT_DEFINES` (`G_<PAÍS>` + `_CLIENT_`). Valores aceitos: os que o
  `Engine/LocalDefine.h` conhece (`BRAZIL`, `USA`, `GERMAN`, `KOR`, `RUSSIA`, `THAI` → `Define_BRA.h`,
  `Define_USA.h`, `Define_GAMIGO.h`, `Define_KOR.h`, `Define_RUS.h`, `Define_THAI.h`); outro valor
  para o configure. O `G_*` liga as features da publisher e alguns ramos (texto RUSSIA/THAI, codepage
  fora do Windows). Os dados de idioma em tempo de execução (`Local\<país>\`, fontes) seguem o
  símbolo persistente `g_iCountry` (BRAZIL = 8, `GlobalDefinition.h`), não o `G_*`. O `LC_REGION` do
  servidor (BRZ, USA...) é outra lista e continua separado. Mortos/sem header: `Define_MEX.h` (nenhum
  `G_MEXICO`; o código testa `G_MAXICO`), `G_EUROPE`, `G_JAPAN`, `G_CHINA`, `G_HONGKONG`, `G_MAL`,
  `G_NETHERLANDS`, `G_RUSSA` (nenhum build os define).
- **`time_t`:** o `_USE_32BIT_TIME_T` saiu do CMake do Windows; `time_t` tem 64 bits nas duas
  plataformas. Nenhum struct gravado em disco/rede tem `time_t` (pacotes e `.lod` usam `int`; os
  `time_t` que existem — `SNotice`, mensagens agendadas do chat, `DamageLogInfo` — ficam só na
  memória), então nenhum tamanho mudou. O que dependia de 32 bits era passar um campo `int`/`LONG`/
  `ULONG` para `localtime((time_t*)&x)` ou `time((time_t*)&x)` (lia/escrevia 8 bytes): agora
  `LocalTime32(SLONG)` (`Base/Types.h`, definida em `Base/Timer.cpp`; lê os 32 bits com sinal, como
  antes) e `x = (INDEX)time(NULL)` (`Player.es`, `UIAutoHelp.cpp`). Cálculos com `time(NULL)` em
  32 bits ganharam cast `(SLONG)` (`UIManager`, `UIRadar`) para dar o mesmo resultado de antes.
  Testes no Windows pendentes: tooltips de expiração (itens com prazo, botões da barra/inventário,
  bolsa de cash, pet stash, armazém, teletransporte, nome do personagem premium), data de presentes
  da loja de cash, notícias, express system, histórico do sindicato, coleção de itens, relógio do
  radar, ressincronização de horário com o servidor (`UIManager`), mensagens agendadas de aviso.
- Compila sem linkar, com clang arm64, `-fms-extensions` e só os avisos de 64 bits ligados
  (`-Wshorten-64-to-32`, `-Wpointer-to-int-cast`, `-Wint-to-pointer-cast`, `-Wformat`...).
  Sem `LC_CHECK_DIRS` compila as partes "portáveis" do Engine (Base, Math, Templates, Brushes,
  World, Terrain, Ska, Models); com `ALL`, todo o Engine, EntitiesMP, GameMP e Shaders.
- `Compat/checkonly/` tem headers **vazios** com nomes do Windows (`windows.h`, `dsound.h`, `io.h`...):
  só existem para cada arquivo passar dos includes e o clang analisar o resto. Os erros de API Win32,
  D3D e asm que sobram são esperados (C3–C5). Um header que falte aparece como `fatal error` e
  esconde o arquivo inteiro: acrescente um stub.
- **`SE1_D3D`:** o Direct3D 8 só compila com essa macro (definida no CMake do Windows). O check e o
  futuro build do Mac não a definem, então os headers do D3D não são mais usados (os stubs `d3d8.h`,
  `d3dx8*.h` saíram de `Compat/checkonly`); só `Compat/d3dx8/d3dx8math.h` entra no include path,
  como matemática pura. Código novo que use D3D vai dentro de `#ifdef SE1_D3D`; fonte só-D3D fica
  inteiro dentro da macro (logo depois do `#include "StdH.h"`), as listas do CMake não mudam.
  Para conferir que o Windows não mudou: resolver só os `#ifdef SE1_D3D` como definidos (ex.:
  `unifdef -DSE1_D3D`, cuidado que ele também simplifica `#if ... && 0`) e comparar com o original.
- **Caixa dos nomes:** o APFS padrão não diferencia maiúsculas, então um include com caixa errada
  passa despercebido aqui. Para conferir, rode o check numa imagem case-sensitive:
  `hdiutil create -size 8g -type SPARSE -fs "Case-sensitive APFS" -volname LCcs x.sparseimage`,
  `hdiutil attach`, `rsync` do repositório (sem `build*`) e o mesmo configure/build. Em 2026-10-18:
  nenhum `fatal error`, 6.978 erros únicos. Includes novos: nome exato do arquivo e `/`.
- `-fms-compatibility` não serve (quebra a libc++ do Mac). Sem `-fms-extensions`, cast de ponteiro
  para inteiro menor vira erro; com ele, aviso.
- Lições do C2 (ver `PORTING_MACOS.md`): o perigo real eram ponteiros guardados em `ULONG`/`INDEX`
  (ordenação de listas, tags da BSP, union de handle de textura, símbolo `pwoCurrentWorld` do shell)
  e campos `unsigned long` em dados de rede/criptografia. Pacotes e registros `.lod` já são portáveis.
  No Mac `INDEX`, `SLONG` e `BOOL` são todos `int`: sobrecargas distintas no Windows colidem lá
  (resolvido com `#ifndef PLATFORM_UNIX` na versão redundante). `PLATFORM_UNIX` é definido em
  `Engine/Base/Base.h` para todo compilador que não seja MSVC.

## Mac: servidor

- Build: `cmake --build build -j8` (configurado com `LC_REGION=BRZ`).
- `scripts/run-server.sh start|stop|status|log <Nome>`; logs em `run/<Servidor>/stdout.log`.
  Os servidores escutam no IP de `en0` (192.168.0.4); o cliente vem de 192.168.0.7.
  Fora da rede de casa (`en0` sem 192.168.0.4; aconteceu em 2026-10-19), só o cliente Mac:
  `LC_PUBLIC_IP=127.0.0.1 scripts/run-server.sh start` e `scripts/sl-dta.py write sl.dta "Mac
  127.0.0.1 4001"` na instalação; o `sl.dta` da rede local fica em `sl.dta.lan`.
- Banco: MariaDB (Homebrew), bancos `newproject_data`, `newproject_db`, `newproject_db_auth`.
  `mysql`/`mariadb` sem senha funcionam no shell. Conta de jogo: `teste`.
  Correções de schema ficam em `server-config/db-fixes.sql` (idempotente) — acrescente lá.
- Mapas do GameServer em `run/GameServer/data/` (não versionado): gerados dos `.wld` do cliente por
  `scripts/export-zone-maps.py [zona ...]`, que roda `ZoneMapExport.exe` (nosso Engine + os exportadores
  do World Editor) no Windows e traz os arquivos. Zona 16 (Egeha_PK) não tem `.wld` no cliente e fica
  com o placeholder plano de `scripts/make-placeholder-maps.py`. Formato (`GameServer/MapAttr.cpp`):
  `.sat` = `uint16` LE com flags `MATT_*`; `.sht` = `uint16` **BE** = altura × 100, linha a linha.
  Atenção: a zona 0 é desenhada pelo cliente a partir de `juno.wld`; os `StartZone_*.sat/.sht` que
  vêm no cliente são da Juno antiga (`startzone.wld`) e não batem com o que o jogador vê.
- Crash do servidor: relatório do macOS em `~/Library/Logs/DiagnosticReports/GameServer-*.ips`
  (JSON; a thread com `triggered` tem a pilha com arquivo e linha).
- `LC_DEBUG_PACKETS` (compile-time) loga pacotes enviados/recebidos com o texto plano.

## Windows: cliente de teste

Host SSH `windows` (192.168.0.7, usuário `schif`, PowerShell como shell remoto). O usuário vê a
tela pelo app "Windows App"; o jogo roda na sessão dele, aberto por tarefa agendada.

| Caminho | O que é |
|---|---|
| `C:\LCSource` | cópia do repositório (recebida por `scripts/windows/sync.sh`) |
| `C:\LCSource-build` | build CMake/Ninja (Release, x86, `LC_CLIENT_COUNTRY=BRAZIL`); saídas em `Bin\` |
| `C:\LCSource-deps` | Boost 1.86 x86, D3DX9 (NuGet), headers D3D8 do MinGW, `build.cmd` (cópia em `scripts/windows/build.cmd`), `stackdump.exe`, `addr2line.exe` |
| `C:\LC-BR2014` | instalação do jogo (assets BR 2014 + alguns `.lod` de 2017, ver abaixo); `Bin\` recebe nossos binários |
| `C:\LC-BR2014\jogar.bat` | inicia o jogo (cópia em `scripts/windows/jogar.bat`); com `C:\LC-BR2014\hw-shaders.txt` presente usa shaders por hardware e liga `LC_DEBUG_SKA`, sem ele força `LC_NO_HW_SHADERS=1` |
| Tarefa agendada `LC Nosso Cliente` | roda `jogar.bat` na sessão interativa (é assim que se abre o jogo remotamente) |
| Área de trabalho: `LC Nosso Cliente.bat` | atalho do usuário (OneDrive\Ambiente de Trabalho), igual ao `jogar.bat` |

Fluxo (do Mac, na raiz do repo):

```
scripts/windows/build.sh [alvos]      # sync + ninja; padrão: Engine Shaders EntitiesMP GameMP Nksp
LC_NO_SYNC=1 scripts/windows/build.sh # compila o que já está lá (para editar a próxima mudança enquanto isso)
scripts/windows/deploy.sh             # copia para C:\LC-BR2014\Bin e abre o jogo
scripts/windows/crash.sh              # último crash do ErrorLog.txt -> função e arquivo:linha
scripts/windows/crash.sh stacks       # pilhas ao vivo (cliente travado, ou parado num MessageBox)
scripts/windows/crash.sh errors       # erros lançados pelo motor (ThrowLog.txt)
scripts/windows/crash.sh shot         # print da tela do Windows -> ./shot.png
scripts/windows/wps.sh <<'EOF' ... EOF  # PowerShell arbitrário no Windows
```

- O login é feito pelo usuário na tela; acompanhe pelo log do servidor (`LOGIN`, `CHAR_LOAD`,
  `CHANGE_STATE ... PLAYING`, `DISCONNECT`). Quando o cliente quebra, o motor grava em
  `C:\LC-BR2014\ErrorLog.txt` e abre um MessageBox; o arquivo fica bloqueado até alguém clicar OK
  ou o processo ser encerrado.
- `crash.sh` resolve endereços com o PDB atual: só é confiável se o binário não mudou desde o crash.
- `ThrowLog.txt` (se existir) recebe toda exceção do motor, falhas de shader e, com
  `LC_DEBUG_SKA=1`, dados de renderização de cada modelo SKA.
- Um build completo leva ~14 min. Com dependências de header funcionando, o incremental é rápido.

### Armadilhas já encontradas no build do Windows

- **Sincronização:** `sync.sh` envia o que mudou desde o último commit sincronizado
  (`.git/lc-windows-last-sync`). Se houver suspeita de divergência, `scripts/windows/sync.sh --verify`
  compara tudo por hash. (Já aconteceu: o Windows ficou com a tabela SEED antiga e o login quebrou.)
  Arquivos apagados aqui também são apagados lá (com os diretórios que ficarem vazios).
- **Horário dos arquivos:** o `sync.sh` extrai com `tar -m` (horário da extração). Antes ele mantinha
  o horário do Mac, e um fonte editado aqui durante um build no Windows podia ficar mais antigo que o
  `.obj` gerado lá: o ninja não recompilava. Se suspeitar disso, escreva um commit antigo em
  `.git/lc-windows-last-sync` e sincronize de novo (os arquivos chegam com horário novo).
- **Dependências de header:** o `cl.exe` está em português e imprime o prefixo do `/showIncludes`
  no codepage do console. O `build.cmd` fixa `chcp 65001` para configure e build usarem o mesmo;
  sem isso o ninja não registra dependências e mudanças em `.h` não recompilam nada.
  Confirme com `ninja -t deps <obj>` (deve listar centenas de headers).
- Se precisar reconfigurar: apague `C:\LCSource-build\CMakeCache.txt` e rode
  `cmd /c "C:\LCSource-deps\build.cmd configure"`.

## Recursos externos no Windows (ainda não testados)

Em 2026-10-17 o usuário baixou o pacote do **tx00100xt** (Alexander Pavlov) com os fontes 2015/2018
do openlastchaos modernizados: `%USERPROFILE%\Downloads\LCSources-20250201\VSProjects` (20 GB) na
máquina `windows`. Por decisão do usuário, **nada disso foi testado ainda**; use como referência e
pergunte antes de adotar algo.

- **Assets completos de clientes em inglês**, já desempacotados: `LC2015\Client\Data` (5 GB) e
  `LC2018\Client\Data` (4,7 GB), com `Local` e `Bin` (`Nksp.exe`, "Client 1776"). Candidatos a
  substituir os dados BR 2014 + `.novo` 2017 (D5 e o item de dados misturados da C0). Não se sabe
  se os `.lod` batem com o nosso fonte: o deles vem do branch `june-2015` do openlastchaos, que tem
  telas de jogo que o nosso não tem (PartyAuto, Affinity, CustomTitle, WildPet, BingoBox...).
- **Bancos por versão**: `DB\2015\ep4_*.sql` e `DB\2018\2018_nov_*.sql` (+ `fix_tables.txt`, caixa
  de nomes de tabela). Com os fontes `LC2018\Client|Server` cobre quase toda a Fase V para 2018.
- **Referência para C2/C3/C8**: cliente 64 bits sem asm; texturas já em 64 bits (`td_ulObject` e
  `td_pulObjects` como `ULONG64` em `Graphics/Texture.h`), útil para o item dos handles do `gfx*`.
- **D3D8 → D3D9 + DXVK 2.5.3 compilada no Engine** (`Engine/Graphics/d3d9`, `dxvk`, `dxso`, `spirv`,
  `wsi` com backend `sdl3`); escolha da API em `Data\etc\GfxAPI.txt` (0 OpenGL, 1 D3D9, 2 Vulkan).
  Possível caminho alternativo no Mac (D3D9 → DXVK → MoltenVK), sem verificar se a DXVK 2.x
  roda sobre o MoltenVK.
- O OpenGL deles **não** avançou: os mesmos `"Not implemented yet"` em `Gfx_wrapper_OpenGL.cpp`.
- Variante mais nova de cada linha: `LC2015\Client\Sources.VS2022.DX9.NoAsm.x64.Z.15.BL.VK.5` e
  `LC2018\Client\Sources.VS2022.DX9.NoAsm.x64.Z.15.BL.VK.5`; as pastas `Z.0`…`Z.14` são etapas
  intermediárias. Servidor Linux 64 bits em `LinuxServer\*.tar.xz` (o nosso já roda no Mac).
- Para ler do Mac: empacote com `tar` via `scripts/windows/wps.sh` e traga com `scp windows:...`,
  extraindo num diretório temporário fora do repositório.

## Problemas conhecidos (e o porquê)

- **Shaders por hardware (resolvido):** o `d3d8.dll` dos Windows atuais (implementado sobre o
  D3D9) recusa vertex shaders que leem componentes de registradores temporários antes de escrevê-los
  (`CreateVertexShader` → `D3DERR_INVALIDCALL`); o runtime do XP aceitava, e os shaders do jogo fazem
  isso (ex.: `mul r1, r1, r7.wwww` com `r1.w` nunca escrito). Sem shader, o modelo não era desenhado.
  O shim (`Compat/d3dx8/d3dx8_shader.cpp`) inicializa cada `rN` usado. `ShaderCheck.exe <arquivo.vsh>`
  testa no D3D8 real se um shader é aceito; precisa rodar na sessão interativa (tarefa agendada),
  pois pelo SSH não há desktop e `CreateDevice` falha. `LC_NO_HW_SHADERS=1` ainda força o caminho
  por CPU, lido em `RM_/TR_AreHardwareShadersAvailable` (os cvars `ska_/ter_bUseHardwareShaders` são
  sobrescritos depois da inicialização).
- **Dados do cliente misturados:** a instalação é BR 2014, mas vários `.lod` foram trocados por
  versões BR 2017 (os originais e as cópias estão em `C:\LC-BR2014\Data\*.novo`) para os tamanhos de
  registro baterem com os structs deste código. Regra: tamanho do registro no arquivo
  = `(tamanho − 4) / quantidade` deve ser igual ao `sizeof` do struct em 32 bits MSVC. Consequências
  já tratadas no código: strings BR usam `%s` onde o código passa números (`CTString::VPrintF` tolera),
  ícones `ItemBtn26/27` não existem (caem na página 0), NPCs que o servidor envia sem modelo no
  cliente são ignorados, lista de dungeons da UI maior que a do código.
  Também (2026-10-18): `UIMap_option.xml` (pacote 2025) espera uma `Map.tex` 512×512, e a BR 2014 é
  256×256 (ícones de loja/outros NPCs vazios no mapa): trocada pela do LC2015 do pacote tx00100xt
  (a antiga ficou em `Data\Interface\Map.tex.anterior`). `ZoneFlag.lod` BR 2014 está no formato
  antigo (um par de flags de 64 bits por índice de NPC, sem o índice); `CMobData::LoadZoneData`
  lê os dois formatos (sem isso os teleportadores não abriam a lista de destinos).
- **Servidor:** tabelas que o código usa e o dump não tem (`t_autobuffs`, `t_raid_zone_data`...),
  dados `ComposeItem`/`EquipExchangeExtend`, NPCs "Not found" — aparecem como ERROR na subida e
  ainda não atrapalharam o jogo.
- Login: cliente e servidor precisam da mesma tabela SEED (`SEED_256_KISA.tab`, KISA padrão) e da
  correção de endianness em `SEED_256_KISA.h` nos dois lados. Sintoma de divergência: o LoginServer
  loga `LOGIN reject` com `version`/`nation` sem sentido.
