# Porte do Last Chaos para macOS

> Estado do ambiente, ferramentas e como continuar: **`HANDOFF.md`**.

Plano de trabalho e checklist do porte. Marque `[x]` quando um item estiver pronto e
anote o commit/PR ao lado quando fizer sentido.

**Alvo:** macOS 14+ em Apple Silicon (arm64), clang/Xcode atual, CMake.
**Fork:** `origin` = PatrickSchifter/LCSource, `upstream` = pwesty/LCSource.

**Como compilar (servidor):**

```sh
scripts/macos-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
ninja -C build            # binários em build/Server Source/
brew install mariadb && brew services start mariadb   # banco local; importar os dumps de openlastchaos/lastchaos-database
scripts/make-placeholder-maps.py && scripts/run-server.sh start
```

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

- [x] **D1. Ordem:** servidor primeiro (decidido em 2026-10-16)
- [x] **D2. Renderizador do cliente** (decidido em 2026-10-17), em três etapas:
  1. **OpenGL** primeiro: reaproveita o backend `GAT_OGL` que o motor já tem (pipeline fixo, perfil legado 2.1 do macOS) com skinning na CPU; bloom e efeitos só-D3D ficam desligados no início
  2. **Metal** depois, como backend novo sobre a interface `gfx*`, com os shaders reescritos em MSL
  3. **Vulkan** por último, para deixar o motor o mais atualizado possível
- [x] **D3. Camada de plataforma do cliente:** SDL2 (janela, contexto OpenGL/Metal, input, entrada de texto/IME, áudio), decidido em 2026-10-17
- [~] **D4. Região/configuração alvo:** qual build (`USA`, `GER`, `KOR`...) e versão de protocolo usar. Padrão provisório: `USA` (`-DLC_REGION`); confirmar quando os assets do cliente Windows chegarem
- [~] **D5. Origem dos assets e do banco:** banco = [openlastchaos/lastchaos-database](https://github.com/openlastchaos/lastchaos-database) (dump oficial de fev/2016, clonado fora do repo); assets virão da instalação Windows do usuário (pendente)
- [ ] **D6. Ferramentas (WorldEditor, Modeler, SKA Studio...):** usam MFC; proposta é ficar fora do escopo

---

## Fase 0 — Preparação do repositório

- [x] Fork em PatrickSchifter/LCSource e clone local com remote `upstream`
- [x] Análise inicial do cliente e do servidor (resultado neste documento)
- [x] Criar este checklist
- [x] Restaurar os 21 arquivos que estão no `.svn/pristine` mas faltam no working copy (+ `LoginServer/IPCheck.cpp` e `GameServer.vcxproj` do servidor):
  - `Client Source/Nksp/Nksp.cpp` (WinMain/loop principal), `GameMP/Map.cpp`, `EngineGUI/EngineGUI.cpp`
  - `Shaders/Common.cpp`, `Shaders/Common.h`, `Shaders/DisplaceShader.cpp`
  - `Engine/SEED_256_KISA/*` (3 arquivos)
  - `include/`: `LCCrypt.h`, `ijl.h`, `vld.h`, `F_*.h`, `SharedMemory_queue.*`, `LCCRC32.h`
- [x] Remover `.svn/` do controle de versão e adicionar `.gitignore` (`.o`, binários, `*.sdf`, `*.suo`, `build/`)
- [x] Remover do git os artefatos compilados (cerca de 120 `.o` ELF 32-bit, binários Linux antigos)
- [ ] Converter os fontes com acentos de CP949 para UTF-8 (549 arquivos, quase tudo em comentários) e normalizar CRLF via `.gitattributes`
- [ ] Revisar `Server Source/docs/`: há arquivos com credenciais de produção em texto puro (já estavam públicos no upstream)
- [x] Estrutura CMake raiz (`CMakeLists.txt`) com opções `LC_BUILD_SERVER` / `LC_BUILD_CLIENT` e `LC_REGION`
- [x] Script de bootstrap de dependências via Homebrew (`scripts/macos-deps.sh`)
- [ ] CI no GitHub Actions (runner `macos-14`) compilando o que já estiver portado

---

## Trilha S — Servidor

### S1. Build e dependências
- [x] CMake para `ShareLib` (lib estática) e para cada servidor, substituindo `Makefile`/`makefile.inc`
- [x] Dependências Homebrew: boost, mysql-client (ou mariadb-connector-c), log4cxx, apr, apr-util, expat, jsoncpp, curl, openssl, botan
- [x] Boost: migrado `io_service` → `io_context` e demais APIs removidas (testado com Boost 1.92)
- [x] Botan 1.10 → Botan 3 (só `check_bcrypt` em `CheckPassword.cpp`, compilado em C++20)
- [x] Sem `-lrt` e sem as `.a` 32-bit de `LIB/` (LCCrypt, LCSha256, LCHttps compilam da fonte em ShareLib)
- [x] Definir as macros de região (`LC_USA` etc.) e `__GAME_SERVER__` no CMake

### S2. Portabilidade de código
- [x] `ShareLib/SignalProcess.cpp`: `uc_mcontext.gregs[REG_EIP]` → `__ss.__pc` (arm64) ou remover
- [-] Trocar `malloc.h` por `stdlib.h`; Windows-isms (`_snprintf`, `stricmp`, `itoa`): não foi necessário, ficam atrás de `#ifdef WIN32`
- [x] Corrigir os erros do clang moderno (`register`, `binary_function`, comparações ponteiro/bool, funções sem `return`, use-after-erase em `Notice.cpp`)
- [ ] Revisar os warnings restantes que indicam bug: `-Wsizeof-pointer-memaccess` (5), `-Wmemset-transposed-args`, `-Wformat-extra-args`, `-Wstring-compare`, `-Wnull-conversion`, `-Wdynamic-class-memaccess`

### S3. 64-bit (LP64)
- [x] `ShareLib/SEED_256_KISA.h`: `typedef unsigned long DWORD` → `uint32_t` (confirmado: em 64-bit gerava ciphertext errado)
- [x] `ShareLib/il_values.h`: `ILUInt32` (SHA-256) → `uint32_t`; SHA-256 validado com vetor de teste oficial
- [ ] `ShareLib/rntype.h`: `tLONG` e similares → tipos de largura fixa
- [ ] Auditar as 84 structs com `#pragma pack` (pacotes de rede): nenhum `long`, `time_t` ou ponteiro dentro
- [ ] Auditar a leitura de arquivos `.bin` binários feita via struct
- [ ] Testes de `static_assert(sizeof(...))` para os pacotes principais

### S4. Componentes (ordem sugerida)
- [x] ShareLib compila
- [x] Messenger compila e linka
- [x] Helper compila e linka
- [x] SubHelper compila e linka
- [x] Connector compila e linka
- [x] LoginServer compila e linka
- [x] GameServer compila e linka (cerca de 180 arquivos)
- [-] Billing, Authentication, CharDBServer (legados ou só Windows; reavaliar se forem necessários)

### S5. Execução local
- [ ] `ShareLib/PrintExcuteInfo.cpp`: compila, mas lê `/proc` em tempo de execução; trocar por `sysctl`
- [ ] Confirmar que XTrap (`dlopen`) e `detours.h` ficam desativados em tempo de execução
- [x] Banco: MariaDB local (Homebrew, só 127.0.0.1) com `newproject_data`, `newproject_db`, `newproject_db_auth` importados do dump
- [x] Configs em `server-config/` (`newStobm.bin` é INI puro), `log.xml` para log4cxx 1.x, `event.ini`
- [x] `scripts/run-server.sh start|stop|status|log` na ordem Messenger → Connector → Helper → SubHelper → GameServer → LoginServer
- [x] **Os 6 servidores sobem e se conectam entre si**; LoginServer enxerga o GameServer (porta 4010)
- [x] Helper: `x = x++` virava loop infinito em C++17 (`ServerRun.cpp`)
- [x] Connector: billing opcional (`[Billing Server] IP=` vazio)
- [x] Mapas reais `.sat`/`.sht` das zonas: `ZoneMapExport` (`Client Source/ZoneMapExport`, exportadores do World Editor sobre o nosso Engine) + `scripts/export-zone-maps.py`; 42 de 43 zonas (a 16, Egeha_PK, não tem `.wld` no cliente). Validado contra os mapas que o cliente BR traz: alturas idênticas e 99,9% de concordância em andabilidade (Juno, Dratan 1F/2F, RoyalRumble)
- [ ] Tabelas que o código usa e o dump não tem: `t_autobuffs`, `t_raid_zone_data`, `t_reports`, `t_key_log` (+ `t_promo_*` só GER)
- [ ] Dados que faltam no GameServer: `ComposeItem`, `EquipExchangeExtend`, NPCs 893/1254 ativos mas "Not found", 6 "Can't Create Item"
- [x] Conta de teste `teste` criada; personagem entra no mundo
- [x] `t_characters.a_guildoutdate` faltava no dump (abortava o GameServer): `server-config/db-fixes.sql`
- [x] Servidor aceita login de cliente Windows (o nosso) pela rede local
- [ ] Higiene: dois OpenSSL carregados (3 e 4, via curl/mysqlclient); `svn_info.h` regenerado a cada configure força rebuild do ShareLib

---

## Trilha C — Cliente

### C0. Cliente Windows a partir do nosso fonte (validação do servidor)
O cliente do LC Brasil Clássico é modificado (SEED com S-boxes/chave próprias, versão 215436, login com HWID/MAC),
então decidimos (2026-10-16) compilar o nosso próprio cliente Windows para testar contra o servidor.
- [x] Windows: VS 2022 Build Tools (x86), CMake, Ninja, WinFlexBison, Boost 1.86 x86 (`C:\LCSource-deps`)
- [x] Dependências fora do repo: D3DX9 (NuGet `Microsoft.DXSDK.D3DX`), headers D3D8 do MinGW-w64 (o Windows SDK não traz mais `d3d8.h`)
- [x] Camada `Compat/d3dx8` (matemática D3DX8 própria; assembler de shader via D3DX9; textura sobre D3D8)
- [x] `Client Source/CMakeLists.txt` (listas de fontes geradas da config BRZLIVE por `scripts/vcxproj-to-cmake.py`)
- [x] XTrap/GameGuard desligados (`LC_NO_XTRAP`, `NO_GAMEGUARD`)
- [x] Engine, Shaders, EntitiesMP, GameMP, Nksp compilando e linkando
- [x] Rodar com os assets do cliente BR 2014 e logar no servidor do Mac; seleção de personagem, entrar no mundo, combate, lojas (2026-10-17)
- [x] **Shaders por hardware**: o D3D8 do Windows atual recusava os vertex shaders que leem registradores temporários não escritos (o de skinning com 4 pesos, entre outros), e os modelos animados sumiam; o shim inicializa os temporários. `ShaderCheck` testa shaders no D3D8 real
- [ ] Dados do cliente misturados (2014 + `.novo` 2017): conferir tamanhos de registro; strings BR usam `%s` para números (tolerado em `CTString::VPrintF`); ícones `ItemBtn26/27` não existem (caem no 0)
- Build no Windows: `build.cmd` fixa `chcp 65001` (o `cl` em português imprime o prefixo do `/showIncludes` no codepage do console; com codepages diferentes entre configure e build o ninja não registrava dependências de header)

### C1. Build e geração de código
- [x] Compilar o `Ecc` nativo e gerar os 108 `.es` → `.cpp/.h/_tables.h` via CMake (macOS e Windows; corrigidos CRLF, vazamento quadrático e pilha do bison)
- [ ] Regenerar no build os parsers do Engine (`Base/Parser.y`, `Scanner.l`, `Ska/smcPars.y`, `smcScan.l`)
- [~] CMake para Engine, EntitiesMP, GameMP, Shaders e o executável (antigo `Nksp`) — existe para Windows/MSVC x86
- [ ] Trocar `LoadLibrary`/`GetProcAddress` de EntitiesMP/Shaders por link estático com registro de classes (ou `dlopen`)
- [ ] Mapear as macros de região (`G_USA`, `G_KOR`...) e remover `_USE_32BIT_TIME_T`
- [ ] Includes com caixa errada (`Console_Internal.h`, `GFX_wrapper_OpenGL.cpp`...); vamos padronizar e não depender de APFS case-insensitive

### C2. Tipos e 64-bit
Plano detalhado e levantamento em `HANDOFF.md` ("Próxima atividade").
- [ ] Procurar `x = x++` (no-op em C++17; já causou loop infinito no Helper)
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
- [x] LCCrypt: compilar a partir de `Server Source/ShareLib/LCCrypt.cpp`
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
- [ ] Etapa OpenGL (D2.1): completar o backend `GAT_OGL` existente no macOS (contexto via SDL2, perfil legado 2.1); shaders programáveis estão como "Not implemented yet", então SKA e terreno usam o caminho sem shader
- [ ] Etapa Metal (D2.2): novo backend `GAT_*` ao lado do D3D8 e do OpenGL
- [ ] Etapa Vulkan (D2.3): novo backend `GAT_*`
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
| 2026-10-16 | Fork, clone, análise inicial, criação do checklist | 5aeb119 |
| 2026-10-16 | Fase 0: arquivos restaurados do SVN, limpeza de `.svn`/`.o`/binários, `.gitignore` | b5fbad4 |
| 2026-10-16 | CMake + ShareLib portado (Boost 1.92, Botan 3, OpenSSL 3, arm64) | 7bd9559 |
| 2026-10-16 | Os 6 servidores compilam e linkam em arm64; correções LP64 no SEED/SHA-256 | 222197b |
| 2026-10-16 | Configs locais, `log.xml` log4cxx 1.x, `run-server.sh` | 76092bb |
| 2026-10-16 | MariaDB + dump importado; os 6 servidores rodando e conectados | cf9da11 |
| 2026-10-16 | Cliente Windows do nosso fonte (CMake/MSVC, shim D3DX8) chega à tela de login; SEED igual nos dois lados | dd8f979 |
| 2026-10-17 | Login, seleção de personagem e entrada no jogo; correções de crash no cliente e no GameServer | 2bbb096 |
| 2026-10-17 | Mapas reais da zona inicial, EXP/`%s`, NPCs sem modelo; modelos visíveis com skinning em software | 65e4d45, 4080b38 |
| 2026-10-17 | Lojas: páginas de ícone `ItemBtn20..30` | 585fef1 |
| 2026-10-17 | Mapas reais de 42 zonas exportados dos `.wld` (`ZoneMapExport`) | 46c09dc |
| 2026-10-17 | Shaders por hardware no cliente Windows (temporários inicializados no shim); confirmado em jogo | 01450ec |
