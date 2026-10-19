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
- [x] **D3. Camada de plataforma do cliente:** SDL3 (janela, contexto OpenGL/Metal, input, entrada de texto/IME, áudio), decidido em 2026-10-17 (era SDL2; trocado no mesmo dia, o `sdl2` do Homebrew já roda sobre o SDL3)
- [~] **D4. Região/configuração alvo:** qual build (`USA`, `GER`, `KOR`...) e versão de protocolo usar. Padrão provisório: `USA` (`-DLC_REGION`); confirmar quando os assets do cliente Windows chegarem
- [~] **D5. Origem dos assets e do banco:** banco = [openlastchaos/lastchaos-database](https://github.com/openlastchaos/lastchaos-database) (dump oficial de fev/2016, clonado fora do repo); assets virão da instalação Windows do usuário (pendente); candidatos em inglês 2015/2018 no Windows, ver `HANDOFF.md` ("Recursos externos no Windows")
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
- [x] `ShareLib/rntype.h`: `tLONG`/`tULONG` → `int32_t`/`uint32_t` (como no build 32-bit original), demais tipos em `<stdint.h>` com `static_assert`. Nenhum desses tipos aparece em pacote ou arquivo (`tLONG`, `tULONG`, `tTIME`, `tSIZE` e `RET_VAL` não têm nenhum uso)
- [x] `ShareLib/NetMsg.h`: `CNetMsg <<`/`>>` com `long`/`unsigned long` gravava e lia **8 bytes** em 64-bit (no original eram 4); agora sempre 4 bytes (testado: `long`, `unsigned long` e `time_t` ocupam 4 bytes cada, big-endian, e voltam iguais). Afetava 26 pontos (achados apagando os operadores e compilando), quase todos `time_t` (`start`, `sendDate`, `remainTime`...) e alguns `long` lidos do cliente (`doFuncEtc.cpp` 2567/2586, `doFuncTrigerEvent.cpp` 21). No Linux 64-bit `int64_t` é `long` e cairia aqui truncado; no macOS é `long long`, sem problema
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
- [x] `ShareLib/PrintExcuteInfo.cpp`: no macOS lê CPU e memória por `sysctl`/`host_statistics64` (testado: Apple M4, 10 CPUs, memória e swap); Linux continua lendo `/proc` (`#ifdef __linux__`)
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
- [x] Regenerar no build os parsers do Engine (`Base/Parser.y`, `Scanner.l`, `Ska/smcPars.y`, `smcScan.l`): `cmake/EngineParsers.cmake`; os gerados antigos (bison 1.25) saíram do repositório. O `win_bison` não pode rodar dois ao mesmo tempo (as saídas se misturam): flex e bison passam por um job pool do ninja de tamanho 1
- [x] CMake para Engine, EntitiesMP, GameMP, Shaders e o executável (antigo `Nksp`): Windows/MSVC x86 e macOS (clang arm64, `.dylib`); primeiro link no Mac em 2026-10-18 (`HANDOFF.md`, "C1 — build no Mac")
- [x] O que o clang recusa e o MSVC aceitava: `CTString` passado por `...` (1.053 casts para `const char*`; `-Werror=non-pod-varargs`), funções `inline` definidas num só `.cpp`, globais definidos no `Engine.h`, especializações de template no header sem `inline`; Ecc `-line` perdia código sem CR
- [x] Testado em jogo no Windows (2026-10-18): as mudanças comuns da C1 (casts de varargs, inline, Ecc), sem diferença
- [ ] Trocar `LoadLibrary`/`GetProcAddress` de EntitiesMP/Shaders por link estático com registro de classes (ou `dlopen`)
- [x] Mapear as macros de região (`G_USA`, `G_KOR`...) e remover `_USE_32BIT_TIME_T`: região numa opção só, `LC_CLIENT_COUNTRY` (Windows, check e o futuro build do Mac); `time_t` de 64 bits também no Windows, com os campos de 32 bits (pacotes, `.lod`, propriedades de entidade) passando por `LocalTime32()`. Detalhes em `HANDOFF.md` ("Verificação do cliente no Mac"). Testado em jogo no Windows em 2026-10-18
- [x] Includes com a caixa do disco e `/` (549 em 453 arquivos, inclusive os `uses` dos `.es`); headers do Windows em minúsculas. Conferido com o check num volume APFS case-sensitive (`d2b016d`, `4563955`)

### C2. Tipos e 64-bit
Verificação no Mac: `-DLC_CLIENT_CHECK_ONLY=ON` compila (sem linkar) o cliente com clang arm64 e
stubs vazios dos headers do Windows (`Compat/checkonly`), só para listar erros e avisos de 64 bits;
`-DLC_CHECK_DIRS=ALL` inclui todo o Engine, EntitiesMP, GameMP e Shaders (ver `cmake/ClientCheck.cmake`).
- [x] Procurar `x = x++` (no-op em C++17; já causou loop infinito no Helper): nenhum no cliente
- [x] `Engine/Base/Types.h`: fora do Windows `SLONG`/`ULONG`/`INDEX`/`RESULT` = `int32_t`/`uint32_t`, `DWORD`/`LONG` de 32 bits, `__int64` = `long long`; `static_assert` dos tamanhos nas duas plataformas. No Windows os tipos não mudam
- [x] Sobrecargas que colidem quando `INDEX`, `SLONG` e `BOOL` são todos `int` (streams, `CNetworkMessage`, `ClearToDefault`); `Clamp*` com limites não deduzidos (`Clamp(i, 0L, ct-1L)` aparece 248 vezes)
- [x] Ponteiros truncados: `CListHead::Sort` (ordenava ponteiros em `ULONG`), tags da BSP/CSG (`size_t`; no disco continuam 32 bits, e o tamanho gravado não usa mais `sizeof(BSPNode)`), `AllocMemoryAligned`, `td_ulObject`/`td_pulObjects` das texturas, `pwoCurrentWorld` (`CShell::GetPointer/SetPointer`), `DXTC.cpp`, hash de ponteiro negativo em `Particles.cpp`; casts de identidade/índice explícitos via `uintptr_t`/`intptr_t`
- [x] Rede: campos `unsigned long` do cabeçalho de pacote (`InputBuffer.cpp`, `FGC_Net_Process.cpp`) e `DWORD` da SEED (`SEED_256_KISA.h`) com 32 bits fora do Windows
- [x] Auditar `Common/Packet/` e `Common/header/def_*.h`: só `int`/`short`/`char`/`bool`/`float`/`__int64` (mesmos tamanhos e alinhamento em MSVC x86 e clang arm64); os `.lod` são lidos com `sizeof(st*)`, não com o da classe derivada. `stAffinity` tem ponteiros, mas o cliente não a usa
- [x] Handles de textura do `gfx*` com largura de ponteiro (2026-10-19): tipo `TEXOBJ` (`Base/Types.h`), `uintptr_t` fora do Windows e `ULONG` no Windows (código igual lá). Programas de shader, consultas de oclusão e buffers dos VBOs continuam `ULONG`; ver `HANDOFF.md` ("C8 — handles de textura")
- [ ] ~1.100 avisos `-Wshorten-64-to-32` (quase todos `strlen`/diferença de ponteiros em `INDEX`, inofensivos abaixo de 2 GB); revisar quando o cliente compilar de verdade
- [ ] MSVC-isms gerais (`stricmp`, `_vsnprintf`, `__try/__except`, `_control87`, `_msize`...) e conformidade C++ restantes: aparecem no check; são da C5
- Testado no cliente Windows em 2026-10-17 (login, mundo, combate, lojas): sem diferença
- Conformidade C++ já corrigida no caminho (vale para o Windows também): `template<>` nas especializações de `Vector`/`Matrix`/`AABBox`, `this->` em bases dependentes (`Templates/`), `template class` nas instanciações explícitas, `DeclareSymbol` aceitando ponteiros de função, `Shell.cpp` gravava strings de arrays persistentes convertendo um `char` em ponteiro

### C3. Assembly x86 (139 blocos em 35 arquivos)
Detalhes em `HANDOFF.md` ("Assembly x86 (C3) — como ficou").
- [x] `USE_PORTABLE_C` em `Base/Base.h` fora de MSVC x86; `ASMOPT` locais amarrados a ele; opção `LC_USE_PORTABLE_C` no CMake do Windows
- [x] Ramos C antigos (RenderModel_View, LayerMixer, TextureEffects, Shader, DrawPort_RenderScene, Gfx_Direct3D, Clipping, Matrix12, Functions.h, Color.h) corrigidos para dar o mesmo resultado do asm
- [x] C novo para os blocos sem fallback: `Graphics.cpp`, `Color.cpp`, `Gfx_Direct3D_Colors.cpp`, `SoundMixer.cpp`, `LayerMixer.cpp`, `Fog.cpp`, `RendMisc.cpp`, `Memory.h`, `Vertex.h`
- [x] `Timer.cpp`/`Profiling.cpp` (`rdtsc` → `steady_clock`), CPUID, `ScanF` (`naked`), breakpoints, `OBFUSCATE`, anti-debug
- [x] Zero erros de asm no check do Mac (`-DLC_CHECK_DIRS=ALL`)
- [x] Testar em jogo no Windows com `LC_USE_PORTABLE_C=ON` (2026-10-17: login, Juno, combate, som; igual ao asm)
- [ ] Checagem automática no CI

### C4. Remover ou isolar dependências só de Windows
Como ficou: `HANDOFF.md` ("C4 — como ficou"). Commits `a325b3a`..`5ff5a2c`.
- [x] Remover `Engine/XBox/*` (e o include em `Engine/StdH.h`)
- [x] nProtect GameGuard (`Nksp/GameGuard.*`, `Engine/GameGuardInterface.h`, blocos `NO_GAMEGUARD`)
- [x] XTrap (`Nksp/XTrap`, `Engine/XTrap`, `xTrapWrapper`, `XTrapInterface`)
- [x] Kalydo (`KALYDO`, `Engine/Kalydo`, `Kalydo/`, `Nksp/Social`, KRFReadLib; manter os ramos `#else`)
- [x] `Engine/Base/Protection.cpp` (`COPY_PROTECTION`=0); manter `Engine/Secure/FileSecure` (checksum portátil usado pelos dados)
- [x] IFeel (stubs), Visual Leak Detector, `MiniDump.cpp` (era código morto, apagado)/`StackDump.cpp`/dbghelp só no Windows
- [x] Intel IJL (screenshot JPEG) → stb_image_write; apagar dxtlib (manter `DXTC.cpp`)
- [x] LCCrypt: compilar a partir de `Server Source/ShareLib/LCCrypt.cpp`
- [x] Limpar `.lib` pré-compilados não usados (todo o `Lib/`), `CDCheck.h`, `FGC_Net.*`
- [x] Testado no cliente Windows em 2026-10-17: build sem erros, login, Juno, screenshot JPEG

### C5. Camada de plataforma
Roteiro (ordem, arquivos, estratégia): `HANDOFF.md` ("C5 — roteiro").
- [x] Janela e loop principal: camada SDL3 (`Engine/Platform/`), eventos viram `MSG`; WinMain/WndProc só no build Win32. Testado no Windows com `LC_PLATFORM_SDL=ON` (2026-10-17)
- [x] Input: teclado e mouse pela camada SDL (sem hooks), `GetAsyncKeyState` emulado no Mac, cursores `.cur` do jogo; joystick fica para depois (sem joystick fora do Windows)
- [x] Entrada de texto e IME: IME do sistema via SDL (composição e candidatos perto da caixa), área de transferência do SDL; `Imm*` só no Windows
- [x] Timers, sleep, `QueryPerformanceCounter`, `timeSetEvent` (emulados sobre `std::chrono`/threads)
- [x] Threads e mutex: `CTCriticalSection` sobre `std::recursive_mutex`, TLS por chaves POSIX
- [x] Sistema de arquivos: caminhos com `\` dentro do motor, convertidos e resolvidos sem caixa na fronteira; streams sem falta de página; `.gro` com `pread`
- [x] Registry: não existe fora do Windows (só ferramentas o usavam); `.ini` com leitor próprio
- [ ] Diretórios do usuário (`~/Library/Application Support/LastChaos`): por ora grava no diretório do jogo, como no Windows (Fase F)
- [x] Testes em jogo dos itens 9–11 no modo SDL e do cliente Win32 com `LC_PLATFORM_SDL=OFF` (2026-10-18)

### C6. Rede
Como ficou: `HANDOFF.md` ("C6 e C7 — como ficou"). Commits `eacb64c`..`ef6097c`.
- [x] Winsock → sockets BSD fora do Windows (`Engine/Network/SocketCompat.h`: nomes do Winsock sobre POSIX, `WSAE*` = `errno`, `SIGPIPE` ignorado); `connect` não bloqueante com `EINPROGRESS`/`SO_ERROR`; `socklen_t`; zlib com `uLongf` de 64 bits. Só no check do Mac; tempo de execução quando o Mac linkar
- [x] `F_Socket` (código morto, só um include) e `SharedMemory` (IPC do launcher, só Windows desde a C5)
- [x] wininet (`Network/Web.cpp`), `SharedWebData` e `ThreadWrapper` só no Windows: páginas web do publisher (quadro de mensagens, notícias, ajuda, loja de cash) que o nosso servidor não tem; fora do Windows o leitor nunca devolve página e `OpenWebPage()` devolve FALSE
- [x] Testes de regressão no Windows (2026-10-18: login, troca de servidor, LoginServer fora do ar e GameServer derrubado com mensagem e sem travar; janelas web não testadas, por decisão do usuário)
- [x] Conexão e login no servidor portado a partir do cliente do Mac (2026-10-19)

### C7. Áudio
Como ficou: `HANDOFF.md` ("C6 e C7 — como ficou"). Commits `3b366e5`..`fd81b47`.
- [x] Saída de som pelo SDL3 fora do Windows (`LC_SOUND_SDL`): o mixer por software do motor empurra o buffer para um `SDL_AudioStream` com a mesma latência (`snd_tmMixAhead`); DS3D por hardware, EAX e waveOut só no Windows. No Windows, opção `LC_SOUND_SDL` (exige `LC_PLATFORM_SDL=ON`) para testar. Testado no Mac com programa de teste (taxa de consumo, sem falta de dados); em jogo ainda não
- [x] Ogg: libvorbisfile linkado direto fora do Windows (o `OggVorbis_File` declarado à mão só batia com a lib 1.0 de 32 bits); no Windows continua o `vorbisfile.dll`
- [x] MP3: dr_mp3 (`3rdparty/dr_libs`) no lugar do `amp11lib.dll` fora do Windows; não se sabe ainda se os assets usam MP3
- [x] Testes em jogo no Windows (2026-10-18): build padrão sem diferença; com `LC_PLATFORM_SDL=ON -DLC_SOUND_SDL=ON` sons normais (música, efeitos, volumes, loading, Alt+Tab)

### C8. Renderização (caminho crítico)
- [x] Direct3D atrás da macro `SE1_D3D` (como no Serious Engine 1 aberto da Croteam): definida só no CMake do Windows (`lc_client_common`), onde o código compilado é o mesmo de antes (conferido resolvendo `SE1_D3D` como definida e comparando byte a byte com o original). Sem ela ficam fora `GAT_D3D`, os membros D3D de `CGfxLibrary`/`CViewPort`/`CRenderTexture`, os `case`/ramos D3D e os fontes só-D3D, que ficam inteiros dentro de `#ifdef SE1_D3D` (`Gfx_Direct3D*.cpp`, `RenderBloom.cpp`; `Gfx_wrapper_Direct3D.cpp` só é incluído sob a macro), sem mexer nas listas geradas do CMake. Sem D3D ainda não há: bloom, render targets (`CRenderTexture::Init` falha), sombras projetadas em textura, programas de shader (as funções `sha*` de registradores ficam vazias), clip plane da água e a mistura da sombra do terreno sem shader. A matemática D3DX (`Compat/d3dx8/d3dx8math.h`, nossa) não depende mais de `d3d8.h` sem a macro e é usada no Mac (altura do terreno, matrizes de sombra). Check do Mac: 6.978 → 5.680 erros únicos, sem erro de D3D (só DirectSound, C7). Depois da C6 e da C7 (2026-10-18): 128 erros únicos, quase todos do OpenGL (`Gfx_OpenGL.cpp`, `GfxLibrary.cpp`, `ViewPort.cpp`), mais `exploration3d`/`Object3D_IO` (ferramentas) e dois `Min` em `RenderModels.cpp`
- [x] Etapa OpenGL (D2.1): completar o backend `GAT_OGL` existente no macOS (perfil legado 2.1); shaders programáveis estão como "Not implemented yet", então SKA e terreno usam o caminho sem shader
  - [x] Contexto via SDL3 (janela com `SDL_WINDOW_OPENGL`, funções por `SDL_GL_GetProcAddress`, swap/vsync pelo SDL, sem gama); check do Mac com 0 erros (2026-10-18)
  - [x] Roda no Mac (2026-10-19): login, Juno, combate, itens, terreno (mapas de sombra e luz dinâmica em fixed function), multitextura, clamp to edge, Retina na tela cheia, vsync; ~20% de um núcleo em jogo. Detalhes em `HANDOFF.md` ("C8 — cliente Mac rodando")
- [ ] Etapa Metal (D2.2): novo backend `GAT_MTL` ao lado do D3D8 e do OpenGL (`LC_GFX_API=metal`); plano e estado em `HANDOFF.md` ("C8 — Metal")
  - [x] M1: dispositivo, quadros, estado, texturas (RGBA8 e DXT), função fixa emulada em MSL; tela de login (2026-10-19)
  - [x] M2: Juno sem programas (terreno, brushes, modelos, partículas, névoa), conferido contra o OpenGL (2026-10-19)
  - [x] M3: programas D3D (`vs.1.1`/`ps.1.x`) traduzidos para MSL, pelo ARB do tradutor do OpenGL (2026-10-19)
  - [ ] M4: render targets (bloom, sombras projetadas, preview da loja), cópia da tela, leituras de profundidade (flares), captura de tela — implementada (2026-10-19, `98defbc`), falta conferir em jogo
  - [ ] M5: Metal como padrão no Mac
- [ ] Etapa Vulkan (D2.3): novo backend `GAT_*`
- [ ] Implementar a interface `gfx*` (estados, texturas, vertex buffers, matrizes) no novo backend
- [ ] Texturas: upload, mipmaps, formatos DXT/compressed
- [x] Animações (skinning SKA), terreno e efeitos na GPU no OpenGL (2026-10-19): o assembly D3D8 traduzido para programas ARB (`Graphics/ShaderAsmToARB.cpp`); malhas estáticas em VBOs. Detalhes em `HANDOFF.md` ("C8 — shaders na GPU e VBOs")
- [ ] Shaders SKA (`Shaders/`: Base, Detail, NormalMap, Reflection, Water...) funcionando no backend novo (no OpenGL rodam pelo tradutor ARB, a água com reflexo e o `texbem` inclusive; o bloom também)
- [x] Reescrever em MSL (Metal) os programas D3D `vs.1.1`/`ps.1.x` (`Graphics/ShaderCode.h`, `Terrain/TRShader_*`, `Effect/*`, `Rendering/RenderBloom.cpp`)
- [ ] Remover a dependência de D3DX (183 usos em 13 arquivos): matrizes, carregamento de textura
- [ ] Fontes e UI (`Engine/Interface`), drawports, render-to-texture
- [x] Suporte a Retina/HiDPI e tela cheia (2026-10-19): resolução em pixels desenhados, cena na densidade cheia com a UI em pontos; detalhes em `HANDOFF.md` ("C8 — Retina no OpenGL")
- [x] Gama, brilho e contraste no macOS (2026-10-19): rampa do monitor em tela cheia; detalhes em `HANDOFF.md` ("C8 — gama no macOS e acertos do Retina")

### C9. Integração e jogo
- [x] Engine inicializa, monta os `.gro` e carrega um mundo (2026-10-19)
- [x] Tela de login renderiza
- [x] Login no servidor e seleção de personagem
- [x] Entrar no mundo, andar, combate, UI, chat; trackpad (dois dedos giram, pinça dá zoom)
- [ ] Desempenho aceitável (perfilar os caminhos C que substituíram o asm)

---

## Fase V — Outras versões (depois do porte)

Objetivo: portar também versões antigas e poder escolher, de forma fácil, qual versão jogar.
Fontes publicados (cada versão é um branch):
- Cliente: [openlastchaos/lastchaos-source-client](https://github.com/openlastchaos/lastchaos-source-client): `2009` ([commit](https://github.com/openlastchaos/lastchaos-source-client/commit/708ca1364d8cba964ffcf1dedc38bd7068667dd4)), `june-2015`, `november-2018`; o commit `e8d45f8` tem o Serious Engine original
- Servidor: [openlastchaos/lastchaos-source-server](https://github.com/openlastchaos/lastchaos-source-server), mesmos branches

- [ ] Medir quanto cada versão difere do nosso fonte (Engine x camada de jogo)
- [ ] Portar a versão 2009 (cliente e servidor) reaplicando as mudanças do Engine
- [ ] Assets e banco de cada versão
- [ ] Seletor de versão (launcher: build do cliente, assets e servidor correspondentes)
- Até lá: mudanças do Engine em commits separados das mudanças de jogo, para poder reaplicá-las

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
| 2026-10-17 | C4: GameGuard, XTrap, Kalydo, proteção contra cópia, MiniDump, VLD, IJL, dxtlib e `.lib` pré-compilados removidos; IFeel em stubs, StackDump só no Windows | a325b3a..5ff5a2c |
| 2026-10-17 | C5: Win32Compat (tipos, tempo, arquivos, INI, DLLs, codepages), camada SDL3 (janela, eventos, input, IME, cursores, mensagens); modo SDL testado no Windows | 6842344..c755cd9 |
| 2026-10-18 | C1: includes com a caixa do disco, `LC_CLIENT_COUNTRY`, `time_t` de 64 bits; servidor: `long` com 4 bytes no `CNetMsg`, sysctl, `rntype.h`; teleportadores (`ZoneFlag.lod`) e ícones do mapa | d2b016d..1be72b3 |
| 2026-10-18 | C7: som pelo SDL3, Ogg com libvorbisfile, MP3 com dr_mp3; C6: sockets BSD, web/ThreadWrapper só no Windows; check do Mac 864 → 128 erros únicos | 3b366e5..ef6097c |
| 2026-10-18 | C1/C8: contexto OpenGL pelo SDL3, CMake do Mac, primeiro link do cliente no Mac (inicia até as fontes, sem dados); `CTString` em varargs, `inline`, Ecc `-line` | 2f57203..1bcb824 |
| 2026-10-19 | C8/C9: cliente Mac joga (login, Juno, combate, itens); terreno, multitextura, clamp, trackpad, 64 bits da rede, desempenho (vsync, névoa, loop inativo) | dd78396..7c678d3 |
| 2026-10-19 | C8: shaders na GPU no OpenGL (assembly D3D8 → programas ARB), terreno e efeitos; VBOs para as malhas estáticas | 369a9d8 |
| 2026-10-19 | C2/C9: conversões float→inteiro sem sinal revisadas no cliente inteiro (AST do clang + UBSan); dano ao jogador em 64 bits; taxas do GameServer de volta às de junho/2015 | e53b9eb, d32de5a..1774afb |
| 2026-10-19 | C8: render targets no OpenGL (FBO) e sombras projetadas dos modelos no terreno e nos brushes | 28e947e |
| 2026-10-19 | C8: bloom, preview da loja, Retina e gama no OpenGL; etapa OpenGL concluída. Servidor: largar item voltou a funcionar | f478ad2..c7e0d31 |
| 2026-10-19 | C2/C8: handles de textura do `gfx*` com largura de ponteiro (`TEXOBJ`), antes do Metal | 40f850b |
| 2026-10-19 | C8: backend Metal, primeira etapa (tela de login com `LC_GFX_API=metal`) | 2c36d7c |
| 2026-10-19 | C8: Metal M2, Juno sem programas conferida contra o OpenGL (billboards no Metal) | e3aec26 |
| 2026-10-19 | C8: Metal M3, programas D3D em MSL (pelo ARB do OpenGL) | 165fd29 |
| 2026-10-19 | C8: Metal M4, render targets, bloom, sombras projetadas, previews, reflexo da água, flares e captura (falta conferir em jogo) | 98defbc |
