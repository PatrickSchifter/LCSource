# Passagem de contexto — porte do Last Chaos para macOS

Leia isto antes de continuar. O plano e o checklist estão em `PORTING_MACOS.md`; aqui está o
**estado do ambiente**, como operar e o que já se aprendeu na prática. Atualizado em 2026-10-17.

## Onde estamos

- Os 6 servidores rodam no Mac (arm64) contra MariaDB local.
- Um cliente **Windows** compilado deste repositório (D3D8, 32 bits, MSVC) loga nesse servidor pela
  rede local, cria e seleciona personagem, entra em Juno (zona 0), anda, luta e usa lojas.
  Ele existe para validar o servidor e conhecer o motor, e é a referência para testar cada passo do porte.
- Cliente Mac: só a preparação. Tipos e 64 bits (C2) feitos; o código do cliente compila no Mac
  apenas no modo de verificação (`LC_CLIENT_CHECK_ONLY`, ver abaixo), sem linkar.
- Branch de trabalho: `macos-port` no fork `git@github.com:PatrickSchifter/LCSource.git` (remote
  `origin`; `upstream` é pwesty/LCSource). Commits terminam com a linha `Co-Authored-By` do Claude.
- O usuário conversa em português; mensagens de commit em inglês.

## Próximos passos sugeridos (em ordem)

Decidido (2026-10-17): renderização em OpenGL primeiro (o backend `GAT_OGL` que o motor já tem,
com skinning na CPU), depois Metal, por último Vulkan; plataforma em SDL2.

1. **C3 — assembly x86** (plano abaixo).
2. C4/C5 (dependências só de Windows e camada de plataforma), depois o restante da C1 e o primeiro
   link no Mac.

## Próxima atividade: assembly x86 (C3) — plano

Objetivo: nenhum `__asm`/`asm()` no build do Mac, com caminhos em C que dão o mesmo resultado.
**O cliente Windows continua sendo a referência**: cada troca é testada lá em jogo. Onde o C for
equivalente, ele pode virar o único caminho também no Windows; onde houver dúvida (desempenho ou
arredondamento), manter o asm só em `_MSC_VER`/`_M_IX86` e o C no resto. Encontrar o que falta
com o check do Mac (`-DLC_CHECK_DIRS=ALL`): `unsupported architecture 'arm64' for MS-style inline
assembly`, `invalid input/output constraint`, `'naked' attribute is not supported`.

Levantamento (2026-10-17, linhas com asm por arquivo):

| Arquivo | Linhas | Como está guardado |
|---|---|---|
| `Engine/Math/Functions.h` | 21 | `USE_PORTABLE_C` / `_MSC_VER` (há C; o ramo GNU `asm()` x86 quebra no clang arm64) |
| `Engine/Graphics/Color.h` | 5 | igual ao Functions.h |
| `Engine/Models/RenderModel_View.cpp` | 12 | `#define ASMOPT 1` local, tem ramo C |
| `Engine/Light/LayerMixer.cpp` | 11 | `ASMOPT`, tem ramo C |
| `Engine/Graphics/TextureEffects.cpp` | 5 | `ASMOPT`, tem ramo C |
| `Engine/Math/Clipping.inl`, `Matrix12.h`, `Graphics/Shader.cpp`, `DrawPort_RenderScene.cpp`, `Gfx_Direct3D.cpp` | 1–6 | `ASMOPT` |
| `Engine/Sound/SoundMixer.cpp` | 4 | `ASMOPT`, mas **sem** ramo C completo (mixer) |
| `Engine/Graphics/Graphics.cpp`, `Graphics/Color.cpp`, `Graphics/Gfx_Direct3D_Colors.cpp` | 7, 7, 12 | sem fallback: reescrever em C |
| `Engine/Graphics/Vertex.h`, `Base/Memory.h`, `LocalDefine.h` | 8, 5, 8 | inline curtos |
| `Engine/Base/Timer.cpp` (`rdtsc`), `Profiling.cpp`, `CTString.cpp` (função `naked`), `Engine.cpp` (CPUID), `Terrain/TRGenerate.cpp` | 1–4 | trocar por `std::chrono`/C; CPUID só no Windows |
| `Base/StackDump.cpp`, `Base/Assert.h`, `ErrorReporting.cpp`, `MemoryTracking.cpp`, `Network/*`, `RendMisc.cpp`, `Fog.cpp`, `RMRender.cpp`, `GameMP/Game.cpp` | 1–2 | conferir um a um (muitos são `int 3`/breakpoint ou debug) |

Passos sugeridos:
1. Um único interruptor em `Engine/Base/Base.h`: definir `USE_PORTABLE_C` fora de MSVC x86
   (`#if !defined(_MSC_VER) || !defined(_M_IX86)`) e trocar os `#define ASMOPT 1` locais por
   `#if defined(USE_PORTABLE_C) ... ASMOPT 0 ... #else ASMOPT 1`. Assim o Windows segue igual e o
   Mac usa C. Recompilar o check e confirmar que os erros de asm desses arquivos somem.
2. `Functions.h`/`Color.h`: o ramo `#else` (GNU `asm()` x86) deve virar o ramo C fora de MSVC.
3. Reescrever os blocos sem fallback (`Graphics.cpp`, `Color.cpp`, `Gfx_Direct3D_Colors.cpp`,
   `SoundMixer.cpp`). Referência: os ports open-source do Serious Engine 1 (SeriousSamClassic /
   SE1 1.10 de Croteam no GitHub) têm C equivalente para quase tudo isso — o motor é o mesmo.
   Conversão de cor e mixer de som são fáceis de testar em jogo (cores da UI, som ambiente/combate).
4. `Timer.cpp`: `rdtsc` → `std::chrono::steady_clock` fora do Windows (o `TEST_GETTIME` em
   `Timer.inl` mostra que o jogo já mede em milissegundos).
5. Testar no Windows com `USE_PORTABLE_C` forçado **também lá** (um `-DUSE_PORTABLE_C` temporário
   no `build.cmd`/CMake) para validar os caminhos C em jogo: login, mundo, modelos animados, terreno,
   efeitos, som. Depois voltar ao asm no Windows se não houver motivo para trocar.
6. Commits pequenos, um tema por commit; atualizar a C3 em `PORTING_MACOS.md`.

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
