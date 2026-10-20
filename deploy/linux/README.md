# Servidores num host Linux (x86_64)

Os servidores das três versões numa imagem Docker, para rodar num servidor remoto. Hoje: a
instância Oracle Cloud `oci-ia-api` (São Paulo, 136.248.115.250, Ubuntu 20.04, 1 GB de RAM) com o
banco na layerbase (MariaDB 11.8). Ver `HANDOFF.md`, "Servidores na nuvem".

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
- No banco: os 9 bancos (`lc2009_*`, `newproject_*`, `lc2018_*`) e `SET GLOBAL sql_mode =
  'NO_ENGINE_SUBSTITUTION'` (a 2015 e a 2018 não aceitam o modo estrito; a 2009 já o tira na sessão).

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

No cliente, o `sl.dta` da instalação aponta para o IP público e a porta do LoginServer da versão
(`scripts/sl-dta.py write <instalação>/sl.dta "<nome> 136.248.115.250 4301 1500 1000"`).
