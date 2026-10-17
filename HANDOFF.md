# Passagem de contexto — porte do Last Chaos para macOS

Leia isto antes de continuar. O plano e o checklist estão em `PORTING_MACOS.md`; aqui está o
**estado do ambiente**, como operar e o que já se aprendeu na prática. Atualizado em 2026-10-17.

## Onde estamos

- Os 6 servidores rodam no Mac (arm64) contra MariaDB local.
- Um cliente **Windows** compilado deste repositório (D3D8, 32 bits, MSVC) loga nesse servidor pela
  rede local, cria e seleciona personagem, entra em Juno (zona 0), anda, luta e usa lojas.
  Ele existe para validar o servidor e conhecer o motor, e é a referência para testar cada passo do porte.
- Cliente Mac: só a preparação. Tipos e 64 bits (C2), assembly x86 (C3) e dependências só de
  Windows (C4) feitos; o código do cliente compila no Mac apenas no modo de verificação
  (`LC_CLIENT_CHECK_ONLY`, ver abaixo), sem linkar.
- Branch de trabalho: `macos-port` no fork `git@github.com:PatrickSchifter/LCSource.git` (remote
  `origin`; `upstream` é pwesty/LCSource). Commits terminam com a linha `Co-Authored-By` do Claude.
- O usuário conversa em português; mensagens de commit em inglês.

## Próximos passos sugeridos (em ordem)

Decidido (2026-10-17): renderização em OpenGL primeiro (o backend `GAT_OGL` que o motor já tem,
com skinning na CPU), depois Metal, por último Vulkan; plataforma em **SDL3** (trocado de SDL2 em
2026-10-17: no Homebrew o `sdl2` já é o sdl2-compat rodando sobre o SDL3).

1. **C5** (camada de plataforma), seguindo o roteiro abaixo; depois o restante da C1 e o primeiro
   link no Mac.

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
