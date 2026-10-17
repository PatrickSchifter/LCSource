# Passagem de contexto — porte do Last Chaos para macOS

Leia isto antes de continuar. O plano e o checklist estão em `PORTING_MACOS.md`; aqui está o
**estado do ambiente**, como operar e o que já se aprendeu na prática. Atualizado em 2026-10-17.

## Onde estamos

- Os 6 servidores rodam no Mac (arm64) contra MariaDB local.
- Um cliente **Windows** compilado deste repositório (D3D8, 32 bits, MSVC) loga nesse servidor pela
  rede local, cria e seleciona personagem, entra em Juno (zona 0), anda, luta e usa lojas.
  Ele existe para validar o servidor e conhecer o motor, e é a referência para testar cada passo do porte.
- Cliente Mac: só a preparação. Tipos e 64 bits (C2) e assembly x86 (C3) feitos; o código do cliente
  compila no Mac apenas no modo de verificação (`LC_CLIENT_CHECK_ONLY`, ver abaixo), sem linkar.
- Branch de trabalho: `macos-port` no fork `git@github.com:PatrickSchifter/LCSource.git` (remote
  `origin`; `upstream` é pwesty/LCSource). Commits terminam com a linha `Co-Authored-By` do Claude.
- O usuário conversa em português; mensagens de commit em inglês.

## Próximos passos sugeridos (em ordem)

Decidido (2026-10-17): renderização em OpenGL primeiro (o backend `GAT_OGL` que o motor já tem,
com skinning na CPU), depois Metal, por último Vulkan; plataforma em SDL2.

1. **C4 — remover/isolar dependências só de Windows**: roteiro pronto na seção abaixo.
2. C5 (camada de plataforma: janela, input, timers, threads, arquivos, registry), depois o restante
   da C1 e o primeiro link no Mac.

## C4 — roteiro (levantado em 2026-10-17)

Caminhos relativos a `Client Source/`. Arquivos em ISO-8859-1/CRLF: buscar com `LC_ALL=C grep -a` e
editar preservando a codificação. A lista de fontes do CMake está em `cmake/*_sources.cmake`.
`CMakeLists.txt:49` (e `cmake/ClientCheck.cmake:52`) definem `LC_NO_XTRAP NO_GAMEGUARD`; `KALYDO`,
`USE_GAMEGUARD` e `COPY_PROTECTION` não estão ligados em lugar nenhum. Ou seja: quase tudo da C4 já é
código morto; o trabalho é **apagar mantendo o ramo `#else` que hoje compila**, para o Windows dar
o mesmo binário. Um commit por item; depois de cada um, check do Mac (`-DLC_CHECK_DIRS=ALL`, sem
erro novo) e build do Windows (`scripts/windows/build.sh`, 0 erros); no fim, deploy e um login.

1. **Engine/XBox** (fácil, começar por aqui): o único uso é `Engine/StdH.h:37`
   `#include <Engine/Xbox/XKeyboard.h>` (header vazio, e a caixa `Xbox`≠`XBox` quebra em FS
   case-sensitive). Tirar o include e apagar o diretório; nenhum `.cpp` dele está no CMake.
2. **GameGuard**: `Nksp/GameGuard.cpp/.h`, `Engine/GameGuardInterface.h`, a lógica de
   `NO_GAMEGUARD`/`USE_GAMEGUARD` em `Engine/LocalDefine.h:149-150` e os blocos `#ifndef NO_GAMEGUARD`
   em `Engine.cpp`, `Contents/Login/UILoginNew.cpp`, `LoginNew.cpp`, `Interface/UILogin.cpp`,
   `Network/CNetwork.h/.cpp` (~9792-9855), `SessionState.h/.cpp`, `SessionStateExten.cpp:1074`
   (`MSG_EX_NPROTECT`: manter o case lendo e descartando o pacote, se o servidor ainda manda),
   `Nksp/Nksp.cpp`. Remover o `.cpp` de `cmake/Nksp_sources.cmake` e o `NO_GAMEGUARD` do CMake.
3. **XTrap**: `Nksp/xTrapWrapper.*`, `Nksp/XTrap/` (com `.lib`), `Engine/XTrap/` (duplicata morta),
   `Engine/XTrapInterface/`, os blocos `XTRAP_SECURE_CKBANG_2010_07_20` em `Define_BRA/USA/RUS/GAMIGO.h`
   e nos usos (`Engine.cpp`, `SessionStateExten.cpp` `MSG_EX_XTRAP`, `SessionStateInfo.cpp`,
   `Nksp.cpp`). Tirar `LC_NO_XTRAP` do CMake no fim.
4. **Kalydo**: apagar cada `#ifdef KALYDO` mantendo o `#else` (loaders de asset em `Texture.cpp`,
   `Ska/Mesh/AnimSet/Skeleton.cpp`, `SoundData.cpp`, `Base/Serial.*`, `CTString.h` e os headers
   correspondentes; `Engine.cpp/.h`, `UIOption.*`, `UILogin.cpp`, `SessionState.*`, `GameMP/Game.cpp`,
   `Map.cpp`, `Nksp.cpp/.h`, `MainWindow.cpp`). Nos `!defined(KALYDO)` (`LocalDefine.h:156`,
   `Engine.cpp:753`, `CNetwork.cpp:2293`, `Nksp.cpp:1215`) manter o corpo e tirar só a condição.
   Apagar `Engine/Kalydo/` (tirar `PackageManager.cpp` de `Engine_sources.cmake`: ele inclui
   `KRFReadLib.h` incondicionalmente), `Kalydo/`, `Nksp/Social/`, `Nksp/Nksp_Kalydo*`, `*.kcproj`,
   `*.kpprofile`.
5. **Protection.cpp** (`#if COPY_PROTECTION`, =0): apagar e manter os `#else` em `Base/Stream.cpp`
   (~282-327, só seta `sys_iCPUMisc=1`) e `Nksp/Nksp.cpp` (~737-781). **Não** mexer em
   `Engine/Secure/FileSecure.*` (checksum portátil exigido por `Entities/Skill.cpp:135`), nem em
   `SEED_256_KISA`/LCCrypt.
6. **IFeel** (force feedback, `LoadLibrary` em runtime): trocar `Base/IFeel.cpp` por stubs no-op
   (as ~45 chamadas em `Player.es`/`PlayerWeapons.es` ficam), ou apagar as chamadas fora dos `.es`.
7. **MiniDump/dbghelp**: `Engine/MiniDump.*` (global `g_MiniDumper` em `Engine.cpp:46,88`) só no
   Windows (`#ifdef _WIN32`); `Base/StackDump.cpp` idem, com stubs fora do Windows (depois: handler
   de sinal + `backtrace()` na C5). `dbghelp` continua linkado só no Windows.
8. **VLD**: ninguém inclui `vld.h`; apagar `include/vld*.h`, `Visual Leak Detector/`, `Lib/VC100/vld*`.
9. **IJL (JPEG)**: só `CImageInfo::SaveJPG_t` (`Graphics/ImageInfo.cpp:402-456`), chamado pelo
   screenshot (`GameMP/Game.cpp:2880`). Trocar por `stb_image_write` (vendorizar o header) ou salvar
   TGA; tirar `IJL15L.LIB` do CMake e `include/ijl.h`.
10. **dxtlib**: `Graphics/dxtlib.h` e o bloco `_MSC_VER < 1600` em `Texture.cpp:34-37,1640-1720`
    (compressão, só editor; hoje devolve NULL). Apagar. **Manter `Graphics/DXTC.cpp`** (descompressor
    DXT em C, usado ao carregar texturas).
11. Limpeza: `.lib` pré-compilados não linkados (`Lib/*.lib`, `Lib/VC100/*` exceto o que ainda for
    usado), `Nksp/CDCheck.h`, `Nksp/FGC_Net.*` (fora do CMake). Conferir antes com `git grep`.

Fica para outras fases: D3D8/D3DX (C8), DirectSound/winmm (C7), Winsock/wininet/F_Socket/SharedMemory
(C6), imm32/registry/janela (C5), ODBC (`odbc32` linkado no Engine/Nksp: ver se algo usa, C5).

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
- Achado à parte: `Stream.cpp` usa `ScanF("%I64x")`, formato só do MSVC (`vsscanf` do Mac não
  entende); `Shader.cpp` ~1461 liga referência não-const a temporário (erro no clang, C1).

## Verificação do cliente no Mac (`LC_CLIENT_CHECK_ONLY`)

```
cmake -S . -B build-client-check -G Ninja -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON -DLC_CLIENT_CHECK_ONLY=ON [-DLC_CHECK_DIRS=ALL]
cmake --build build-client-check -- -k 0 2>&1 | tee check.log
```

- Compila sem linkar, com clang arm64, `-fms-extensions` e só os avisos de 64 bits ligados
  (`-Wshorten-64-to-32`, `-Wpointer-to-int-cast`, `-Wint-to-pointer-cast`, `-Wformat`...).
  Sem `LC_CHECK_DIRS` compila as partes "portáveis" do Engine (Base, Math, Templates, Brushes,
  World, Terrain, Ska, Models); com `ALL`, todo o Engine, EntitiesMP, GameMP e Shaders.
- `Compat/checkonly/` tem headers **vazios** com nomes do Windows (`windows.h`, `d3d8.h`, `io.h`...):
  só existem para cada arquivo passar dos includes e o clang analisar o resto. Os erros de API Win32,
  D3D e asm que sobram são esperados (C3–C5). Um header que falte aparece como `fatal error` e
  esconde o arquivo inteiro: acrescente um stub.
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
- **Dependências de header:** o `cl.exe` está em português e imprime o prefixo do `/showIncludes`
  no codepage do console. O `build.cmd` fixa `chcp 65001` para configure e build usarem o mesmo;
  sem isso o ninja não registra dependências e mudanças em `.h` não recompilam nada.
  Confirme com `ninja -t deps <obj>` (deve listar centenas de headers).
- Se precisar reconfigurar: apague `C:\LCSource-build\CMakeCache.txt` e rode
  `cmd /c "C:\LCSource-deps\build.cmd configure"`.

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
- **Servidor:** tabelas que o código usa e o dump não tem (`t_autobuffs`, `t_raid_zone_data`...),
  dados `ComposeItem`/`EquipExchangeExtend`, NPCs "Not found" — aparecem como ERROR na subida e
  ainda não atrapalharam o jogo.
- Login: cliente e servidor precisam da mesma tabela SEED (`SEED_256_KISA.tab`, KISA padrão) e da
  correção de endianness em `SEED_256_KISA.h` nos dois lados. Sintoma de divergência: o LoginServer
  loga `LOGIN reject` com `version`/`nation` sem sentido.
