# Servidores num host Linux (x86_64)

Os servidores das três versões numa imagem Docker, para rodar num servidor remoto. Hoje: a
instância Oracle Cloud `oci-ia-api` (São Paulo, 136.248.115.250, Ubuntu 20.04, x86_64, 1 GB de
RAM), com o MariaDB do jogo nela mesma (`compose.yml`, serviço `db`) e um backup a cada 6 horas no
MariaDB da layerbase (`lc-backup.sh`, `lc-backup.timer`). Ver `HANDOFF.md`, "Servidores na nuvem".

**1 GB não comporta as três versões e o banco** (o sistema troca memória com o disco o tempo
todo, e o banco vai para o swap): por ora só a 2009 roda (`docker compose up -d db v2009`); a 2015 e
a 2018 sobem com `docker compose up -d v2015` / `v2018` depois de parar a outra. O plano é uma
instância Ampere A1 (Always Free: nesta conta, até 2 OCPU e 12 GB), que em 2026-10-20 dava "Out of
capacity" em São Paulo (a Micro também); na A1 (arm64) a imagem é montada sem a cross-compilação.

## Montar a imagem (no Mac)

```sh
colima start lc --vm-type vz --vz-rosetta --cpu 6 --memory 8   # uma vez; o build não usa a emulação
docker buildx build --platform linux/amd64 --build-arg DEBIAN_MIRROR=debian.c3sl.ufpr.br \
  -f deploy/linux/Dockerfile -t lastchaos-servers --load .
```

O build roda nativo (arm64) e cross-compila para x86_64 (`x86_64-linux-gnu.cmake`): sob QEMU o gcc
quebrava. Para iterar em erros de compilação, `--target deps` dá a imagem só com o compilador e as
bibliotecas, e os fontes podem ser montados com `-v "$PWD":/repo` (cuidado: o volume do Mac não
diferencia maiúsculas, o `COPY` da imagem sim).

## Mandar para o host

```sh
docker save lastchaos-servers | gzip -1 | ssh <host> 'sudo docker load'
# configs e mapas de cada versão (sem logs), em /srv/lc/<versão>/{server-config,run}
for v in v2009 v2015 v2018; do
  COPYFILE_DISABLE=1 tar -C $v -czf - --exclude run/LogFiles --exclude stdout.log --exclude pid \
    --exclude newStobm.bin --exclude run/backup-newjuno run server-config \
    | ssh <host> "mkdir -p /srv/lc/$v && tar -xzf - -C /srv/lc/$v"
done
scp deploy/linux/compose.yml <host>:/srv/lc/
```

No host, uma vez:
- `/srv/lc/lc.env` (modo 600, fora do git): `LC_PUBLIC_IP`, `LC_DB_HOST`, `LC_DB_PORT`, `LC_DB_USER`,
  `LC_DB_PASSWORD`.
- `lc-network.sh` em `/usr/local/sbin` e o serviço `lc-network.service` (oneshot, no boot, com o IP
  público e o privado como argumentos): o IP público no `lo`, DNAT das portas do jogo e o firewall.
  Na Oracle, a lista de segurança da sub-rede também precisa liberar as portas (TCP de 0.0.0.0/0):
  4001/4010 (2015), 4201/4210 (2009), 4301/4310 (2018).
- `/srv/lc/backup.cnf` (modo 600): o login do banco da nuvem para o backup (`[client]` com host,
  port, user, password, `ssl=1`); `lc-backup.sh` em `/usr/local/sbin` e o `lc-backup.timer`.
- No banco (`db`): o usuário do jogo (`LC_DB_USER`, com `ALL` em `lc20%` e `newproject%`) e os 9
  bancos (`lc2009_*`, `newproject_*`, `lc2018_*`), carregados de um `mysqldump` do Mac.

## Operar

```sh
cd /srv/lc
sudo docker compose up -d              # as três versões; ou: up -d v2018
sudo docker compose ps
sudo docker compose logs -f v2015      # a saída do lc-servers.sh
tail -f /srv/lc/v2015/run/GameServer/stdout.log
sudo docker compose restart v2009
```

`lc-servers.sh` sobe os servidores de uma versão na ordem certa; se um cai, ele para os outros e o
Docker reinicia a versão inteira. Os servidores conectam ao banco com a porta 0, que o cliente
MariaDB troca por `MYSQL_TCP_PORT` (`LC_DB_PORT`).

Backup: `sudo lc-backup.sh` (ou `--local`); os dumps ficam em `/srv/lc/backups` (os 3 últimos).

No Mac, o launcher joga na nuvem com "<versão> (nuvem)" na lista (`launcher.sh 2009 nuvem`): não
sobe nada local e aponta o `sl.dta` para `LC_CLOUD_HOST` (padrão 136.248.115.250).

Achados do Linux (já corrigidos no código): nomes de tabela com maiúscula no código e
minúsculas nos bancos (o MariaDB do Linux diferencia; o banco local usa
`lower_case_table_names=1` de qualquer forma), e a ordem de inicialização dos pools estáticos do
`MemoryPoolBase.h` (o Messenger da 2015/2018 caía ao abrir).
