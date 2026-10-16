# Porte do Last Chaos para macOS

Plano de trabalho e checklist do porte. Marque `[x]` quando um item estiver pronto e
anote o commit/PR ao lado quando fizer sentido.

**Alvo:** macOS 14+ em Apple Silicon (arm64), clang/Xcode atual, CMake.
**Fork:** `origin` = PatrickSchifter/LCSource, `upstream` = pwesty/LCSource.

Legenda: `[ ]` pendente · `[~]` em andamento · `[x]` pronto · `[-]` descartado

---

## Visão geral

| Parte | Base | Situação original | Dificuldade |
|---|---|---|---|
| `Server Source` | C++ próprio, boost::asio, MySQL | Já compilava em Linux (CentOS 5.9, gcc 4.4, **32-bit**) | Média |
| `Client Source` | Serious Engine 1 (Croteam) + LC | Só Windows: VS2010, Win32, **Direct3D 8**, DirectSound, ~4.900 linhas de asm x86 | Alta |

Faltam no repositório: **assets do jogo** (`.gro`, `.tex`, `.wld`, `.ogg`...), **schema do
banco** (nenhum `.sql`) e **arquivos de config** do servidor (`newStobm.bin`, `log.xml`,
`data/`). Eles precisam vir de uma instalação existente.

---

## Decisões em aberto

- [ ] **D1. Ordem:** servidor primeiro (mais rápido de ver rodando e serve para testar o cliente) ou cliente primeiro?
- [ ] **D2. Renderizador do cliente:** Metal nativo · OpenGL 4.1 core (deprecado no macOS, mas mais simples) · Vulkan via MoltenVK
- [ ] **D3. Camada de plataforma do cliente:** SDL2/SDL3 (janela, input, IME, áudio, threads) ou Cocoa nativo
- [ ] **D4. Região/configuração alvo:** qual build (`USA`, `GER`, `KOR`...) e versão de protocolo usar
- [ ] **D5. Origem dos assets e do banco:** de qual cliente/servidor vamos tirar os dados para testar
- [ ] **D6. Ferramentas (WorldEditor, Modeler, SKA Studio...):** usam MFC; proposta é ficar fora do escopo

---

## Fase 0 — Preparação do repositório

- [x] Fork em PatrickSchifter/LCSource e clone local com remote `upstream`
- [x] Análise inicial do cliente e do servidor (resultado neste documento)
- [x] Criar este checklist
- [ ] Restaurar os 21 arquivos que estão no `.svn/pristine` mas faltam no working copy:
  - `Client Source/Nksp/Nksp.cpp` (WinMain/loop principal), `GameMP/Map.cpp`, `EngineGUI/EngineGUI.cpp`
  - `Shaders/Common.cpp`, `Shaders/Common.h`, `Shaders/DisplaceShader.cpp`
  - `Engine/SEED_256_KISA/*` (3 arquivos)
  - `include/`: `LCCrypt.h`, `ijl.h`, `vld.h`, `F_*.h`, `SharedMemory_queue.*`, `LCCRC32.h`
- [ ] Remover `.svn/` do controle de versão e adicionar `.gitignore` (`.o`, binários, `*.sdf`, `*.suo`, `build/`)
- [ ] Remover do git os artefatos compilados (cerca de 120 `.o` ELF 32-bit, binários Linux antigos)
- [ ] Converter os fontes com acentos de CP949 para UTF-8 (549 arquivos, quase tudo em comentários) e normalizar CRLF via `.gitattributes`
- [ ] Revisar `Server Source/docs/`: há arquivos com credenciais de produção em texto puro (já estavam públicos no upstream)
- [ ] Estrutura CMake raiz (`CMakeLists.txt`) com opções `LC_BUILD_SERVER` / `LC_BUILD_CLIENT` e `LC_REGION`
- [ ] Script de bootstrap de dependências via Homebrew (`scripts/macos-deps.sh`)
- [ ] CI no GitHub Actions (runner `macos-14`) compilando o que já estiver portado

---

## Trilha S — Servidor

### S1. Build e dependências
- [ ] CMake para `ShareLib` (lib estática) e para cada servidor, substituindo `Makefile`/`makefile.inc`
- [ ] Dependências Homebrew: boost, mysql-client (ou mariadb-connector-c), log4cxx, apr, apr-util, expat, jsoncpp, curl, openssl, botan
- [ ] Boost: fixar ≤1.86 **ou** migrar `io_service` → `io_context` (54 ocorrências em ShareLib)
- [ ] Botan 1.10 → Botan 2/3 (Connector, LoginServer)
- [ ] Remover `-lrt` e as `.a` 32-bit pré-compiladas em `LIB/` (LCCrypt, LCSha256, LCHttps já têm fonte em ShareLib)
- [ ] Definir as macros de região (`LC_USA` etc.) e `__GAME_SERVER__` no CMake

### S2. Portabilidade de código
- [ ] `ShareLib/SignalProcess.cpp`: `uc_mcontext.gregs[REG_EIP]` → `__ss.__pc` (arm64) ou remover
- [ ] `ShareLib/PrintExcuteInfo.cpp`: `/proc/cpuinfo`, `/proc/meminfo` → `sysctl`
- [ ] Trocar `malloc.h` por `stdlib.h`; Windows-isms (`_snprintf`, `stricmp`, `itoa`) em cerca de 21 arquivos
- [ ] Desativar XTrap (`GameServer/XTrap4Server.cpp`) e `detours.h` (use o equivalente aos alvos `noxtrap`)
- [ ] Corrigir os avisos e erros do clang moderno (C++ da época do gcc 4: `register`, string literal → `char*`, conversões implícitas)

### S3. 64-bit (LP64)
- [ ] `ShareLib/SEED_256_KISA.h`: `typedef unsigned long DWORD` → `uint32_t` (quebra a criptografia em 64-bit)
- [ ] `ShareLib/rntype.h`: `tLONG` e similares → tipos de largura fixa
- [ ] Auditar as 84 structs com `#pragma pack` (pacotes de rede): nenhum `long`, `time_t` ou ponteiro dentro
- [ ] Auditar a leitura de arquivos `.bin` binários feita via struct
- [ ] Testes de `static_assert(sizeof(...))` para os pacotes principais

### S4. Componentes (ordem sugerida)
- [ ] ShareLib compila
- [ ] Messenger
- [ ] Helper
- [ ] SubHelper
- [ ] Connector
- [ ] LoginServer
- [ ] GameServer (cerca de 300 arquivos)
- [-] Billing, Authentication, CharDBServer (legados ou só Windows; reavaliar se forem necessários)

### S5. Execução local
- [ ] Obter ou reconstruir o schema MySQL (bancos data, user/auth, char)
- [ ] Gerar `newStobm.bin` (INI ofuscado), `log.xml`, `data/event.ini`
- [ ] Script `scripts/run-server.sh` subindo na ordem MySQL → Messenger → Connector → Helper → SubHelper → GameServer → LoginServer
- [ ] Servidor sobe e aceita login de um cliente Windows original (teste de protocolo)

---

## Trilha C — Cliente

### C1. Build e geração de código
- [ ] Compilar o `Ecc` nativo (flex/bison do Homebrew) e gerar os 108 `.es` → `.cpp/.h/_tables.h` via CMake
- [ ] Regenerar no build os parsers do Engine (`Base/Parser.y`, `Scanner.l`, `Ska/smcPars.y`, `smcScan.l`)
- [ ] CMake para Engine, EntitiesMP, GameMP, Shaders e o executável (antigo `Nksp`)
- [ ] Trocar `LoadLibrary`/`GetProcAddress` de EntitiesMP/Shaders por link estático com registro de classes (ou `dlopen`)
- [ ] Mapear as macros de região (`G_USA`, `G_KOR`...) e remover `_USE_32BIT_TIME_T`
- [ ] Includes com caixa errada (`Console_Internal.h`, `GFX_wrapper_OpenGL.cpp`...); vamos padronizar e não depender de APFS case-insensitive

### C2. Tipos e 64-bit
- [ ] `Engine/Base/Types.h`: `SLONG`, `ULONG`, `INDEX` → `int32_t`/`uint32_t` (**crítico**: todos os streams dependem disso)
- [ ] Cerca de 185 casts de ponteiro para `ULONG`/`SLONG`/`INDEX` → `uintptr_t`/`intptr_t`
- [ ] `__int64` (107 arquivos) → `int64_t`
- [ ] Auditar `#pragma pack` em `Common/Packet/` (38) e `Common/header/def_*.h` (11), cruzando com o servidor
- [ ] MSVC-isms gerais: `__forceinline`, `_stricmp`, `_snprintf`, `__try/__except` (2 arquivos), `__declspec`

### C3. Assembly x86 (139 blocos em 35 arquivos)
- [ ] Forçar `USE_PORTABLE_C` e desligar `ASMOPT` (os caminhos C já existem em RenderModel_View, LayerMixer, TextureEffects, Shader, Clipping, DrawPort_RenderScene, Functions.h, Color.h)
- [ ] Reescrever em C os blocos sem fallback: `Graphics/Graphics.cpp`, `Graphics/Color.cpp`, `Graphics/Gfx_Direct3D_Colors.cpp`, `Sound/SoundMixer.cpp` (referência: ports open-source do SE1 1.10, como o SeriousSamClassic)
- [ ] `Base/Timer.cpp` (`rdtsc`), `Base/Memory.h`, `Math/Matrix12.h`
- [ ] Zero `__asm` no build macOS (checagem automática no CI)

### C4. Remover ou isolar dependências só de Windows
- [ ] nProtect GameGuard (`Nksp/GameGuard.*`, `NPGameLib.h`)
- [ ] XTrap (`Nksp/XTrap`, `Engine/XTrap`, `xTrapWrapper`)
- [ ] Kalydo (`KALYDO`, `Engine/Kalydo`, `Nksp/Social`, KRFReadLib, presente em 27 arquivos)
- [ ] `Engine/Base/Protection.cpp`, `Engine/Secure/FileSecure` (manter o que for necessário para ler os assets)
- [ ] IFeel, Visual Leak Detector, `MiniDump.cpp`/dbghelp
- [ ] Intel IJL (JPEG) → stb_image; dxtlib → decodificador DXT próprio ou GPU nativa
- [ ] LCCrypt: compilar a partir de `Server Source/ShareLib/LCCrypt.cpp`
- [ ] Remover `Engine/XBox/*`

### C5. Camada de plataforma
- [ ] Janela e loop principal (substituir `Nksp/MainWindow.cpp`, WinMain e HWND, que aparece em 44 arquivos)
- [ ] Input: teclado e mouse (`Engine/Base/Input.cpp`: `GetAsyncKeyState`, hooks, `ClipCursor`), joystick
- [ ] Entrada de texto e IME (`UIEditBox.cpp`, 36 chamadas `Imm*`)
- [ ] Timers (`QueryPerformanceCounter`, `timeSetEvent`) → `mach_absolute_time`/`std::chrono`
- [ ] Threads (`CreateThread`/`_beginthread`) → `std::thread`
- [ ] Sistema de arquivos: `_findfirst` → `opendir`/`std::filesystem`; separador `\\` (2.413 literais em 278 arquivos) normalizado em uma única função de abertura
- [ ] Registry (`Base/Registry.cpp`, `MultiMonitor.cpp`) → arquivo de preferências
- [ ] Diretórios do usuário (`~/Library/Application Support/LastChaos`)

### C6. Rede
- [ ] Winsock → sockets POSIX (23 arquivos, `Engine/Network/*`)
- [ ] `F_Socket` e `SharedMemory` portáveis
- [ ] wininet (`Network/Web.cpp`) → libcurl/NSURLSession, ou desativar
- [ ] Conexão e login no servidor portado (Trilha S)

### C7. Áudio
- [ ] Substituir DirectSound/EAX/waveOut (`Sound/SoundLibrary.cpp`) por um backend SDL/CoreAudio/OpenAL
- [ ] Ogg: linkar libvorbis direto (hoje carrega `vorbisfile.dll`)
- [ ] MP3: trocar `amp11lib.dll` por minimp3/dr_mp3

### C8. Renderização (caminho crítico)
- [ ] Escolher a API (ver D2) e criar um novo backend `GAT_*` ao lado do D3D8 e do OpenGL legado
- [ ] Implementar a interface `gfx*` (estados, texturas, vertex buffers, matrizes) no novo backend
- [ ] Texturas: upload, mipmaps, formatos DXT/compressed
- [ ] Shaders SKA (`Shaders/`: Base, Detail, NormalMap, Reflection, Water...) funcionando no backend novo
- [ ] Reescrever em GLSL/MSL os programas D3D `vs.1.1`/`ps.1.x` (`Graphics/ShaderCode.h`, `Terrain/TRShader_*`, `Effect/*`, `Rendering/RenderBloom.cpp`)
- [ ] Remover a dependência de D3DX (183 usos em 13 arquivos): matrizes, carregamento de textura
- [ ] Fontes e UI (`Engine/Interface`), drawports, render-to-texture
- [ ] Suporte a Retina/HiDPI e tela cheia

### C9. Integração e jogo
- [ ] Engine inicializa, monta os `.gro` e carrega um mundo
- [ ] Tela de login renderiza
- [ ] Login no servidor e seleção de personagem
- [ ] Entrar no mundo, andar, combate, UI, chat
- [ ] Desempenho aceitável (perfilar os caminhos C que substituíram o asm)

---

## Fase F — Empacotamento

- [ ] `.app` bundle (Info.plist, ícone, assets em `Resources` ou caminho externo)
- [ ] Assinatura de código e notarização (opcional, para distribuir)
- [ ] Servidor: script/launchd ou Docker para subir tudo localmente
- [ ] README do fork com instruções de build no macOS

---

## Registro de progresso

| Data | O que foi feito | Commit |
|---|---|---|
| 2026-10-16 | Fork, clone, análise inicial, criação do checklist | — |
