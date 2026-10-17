# Passagem de contexto — porte do Last Chaos para macOS

Leia isto antes de continuar. O plano e o checklist estão em `PORTING_MACOS.md`; aqui está o
**estado do ambiente**, como operar e o que já se aprendeu na prática. Atualizado em 2026-10-17.

## Onde estamos

- Os 6 servidores rodam no Mac (arm64) contra MariaDB local.
- Um cliente **Windows** compilado deste repositório (D3D8, 32 bits, MSVC) loga nesse servidor pela
  rede local, cria e seleciona personagem, entra em Juno (zona 0), anda, luta e usa lojas.
  Ele existe para validar o servidor e conhecer o motor; **o cliente nativo para Mac ainda não começou**.
- Branch de trabalho: `macos-port` no fork `git@github.com:PatrickSchifter/LCSource.git` (remote
  `origin`; `upstream` é pwesty/LCSource). Commits terminam com a linha `Co-Authored-By` do Claude.
- O usuário conversa em português; mensagens de commit em inglês.

## Próximos passos sugeridos (em ordem)

1. **Mapas reais das 40 zonas restantes** (ver tabela em `PORTING_MACOS.md`, S5): o servidor usa
   mapas planos de altura 0 (o jogador fica embaixo do terreno; tudo é "andável"). Só as zonas 0, 26
   e 38 têm mapa real, vindo de `.sat/.sht` que o cliente BR traz (`scripts/convert-client-maps.py`).
   Falta portar o exportador do World Editor (`Engine/World/WorldIO.cpp`, `Engine/Terrain/TRGenerate.cpp`)
   e gerar a partir dos `.wld` em `C:\LC-BR2014\Data\World\...`. Formato que o GameServer lê
   (`Server Source/GameServer/MapAttr.cpp`): `.sat` = `uint16` little-endian com flags `MATT_*`
   (`ShareLib/Config.h`), `.sht` = `uint16` **big-endian** = altura × 100, linha a linha (`z*W + x`).
2. Skinning por hardware (abaixo, "Problemas conhecidos").
3. Começar o cliente Mac: decisões D2 (Metal ou OpenGL) e D3 (SDL ou Cocoa) ainda estão em aberto
   com o usuário; o primeiro passo técnico é 64-bit no Engine (`PORTING_MACOS.md`, C2).

## Mac: servidor

- Build: `cmake --build build -j8` (configurado com `LC_REGION=BRZ`).
- `scripts/run-server.sh start|stop|status|log <Nome>`; logs em `run/<Servidor>/stdout.log`.
  Os servidores escutam no IP de `en0` (192.168.0.4); o cliente vem de 192.168.0.7.
- Banco: MariaDB (Homebrew), bancos `newproject_data`, `newproject_db`, `newproject_db_auth`.
  `mysql`/`mariadb` sem senha funcionam no shell. Conta de jogo: `teste`.
  Correções de schema ficam em `server-config/db-fixes.sql` (idempotente) — acrescente lá.
- Mapas do GameServer em `run/GameServer/data/` (não versionado): placeholders de
  `scripts/make-placeholder-maps.py`, sobrescritos pelos reais de `scripts/convert-client-maps.py`.
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
| `C:\LC-BR2014\jogar.bat` | inicia o jogo; **precisa** de `set LC_NO_HW_SHADERS=1` |
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

- **Skinning por hardware:** modelos animados (jogadores, monstros, a maioria dos NPCs) ficam
  invisíveis quando usam vertex programs `vs.1.1` montados pelo shim `Compat/d3dx8/d3dx8_shader.cpp`
  (D3DX9 + remoção dos `dcl_`). Com `LC_NO_HW_SHADERS=1` (lido em `RM_/TR_AreHardwareShadersAvailable`)
  o skinning é feito na CPU e tudo aparece. Atenção: os cvars `ska_/ter_bUseHardwareShaders` são
  sobrescritos depois da inicialização, por isso a chave fica nessas funções.
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
