# Goodix v0.7 — CI KUnit em UML

O workflow independente x86_64/QEMU para KCSAN strict está documentado em
[KCSAN-QEMU.md](KCSAN-QEMU.md). A matriz UML da cópia pública usa Basic, lockdep e KASAN.

O workflow é manual e usa apenas runners **GitHub-hosted ubuntu-24.04**. Não
configurar como self-hosted no LG Gram. Nenhum teste opera sensor, SPI/GPIO/IRQ
física, energia, firmware, PSK/OTP ou flash. Os fontes preservados não têm backend
físico, spi_driver, aliases ACPI ou opção de ativação de hardware.

## Publicar e executar

1. Coloque `.github/workflows/goodix-kunit.yml`, `ci/goodix/` e a árvore extraída
   `research/offline-kunit/` no repositório GitHub. O ZIP sozinho não basta.
   Não inclua `build/`, objetos ou caches. Os scripts não alteram essa árvore.
2. Publique o workflow na branch padrão e habilite GitHub Actions no repositório.
   Não são necessários secrets próprios; o workflow solicita somente contents:read.
3. Abra **Actions → Goodix v0.7 offline KUnit UML → Run workflow**. Selecione a
   branch com os arquivos. `debug_profiles=false` é o padrão; mantenha-o assim até validar basic.
4. Aguarde o job `basic`. Somente depois de seu sucesso serão iniciados os jobs
   separados `lockdep` e `kasan`, com fail-fast desabilitado.
5. Baixe os artefatos `goodix-uml-<perfil>-<run_id>-<attempt>`. Leia `status.json`
   antes de interpretar o restante. Não use apenas o sinal verde do build.

Opcionalmente, numa máquina com gh autenticado ao seu repositório:

```bash
gh workflow run goodix-kunit.yml -f debug_profiles=false
gh run list --workflow goodix-kunit.yml
gh run view RUN_ID --log
gh run download RUN_ID --dir evidencias-goodix
```

Os [resultados históricos preservados](../../docs/evidence/README.md) documentam as execuções anteriores.

## Fonte e fases

O pin está em `kernel.json`: Linux upstream **v6.12**, commit
`adc218676eef25575469234709c2d87185ca223a`, do repositório oficial
`https://github.com/torvalds/linux.git`. Não se usa a ponta móvel de um branch.
O fetch exige esse commit, compara HEAD e executa git fsck. A procedência depende
do remoto HTTPS e do hash fixado; não há alegação de verificação GPG do tag.
A versão não é o kernel Ubuntu 7.0.0-42 e não certifica compatibilidade com ele.

O runner instala dependências UML; versões e saída apt são registradas. A árvore
Linux fica em RUNNER_TEMP, independente dos fontes Goodix. A integração copia os
12 arquivos necessários, verifica hashes e guarda o patch no kernel. Não altera
C, headers ou fixtures originais. Configuração, build, auditoria do objeto e
execução UML são fases distintas. A execução tem timeout interno de 120 s e
limite externo de 300 s, com término do grupo de processos em caso de travamento.
TMPDIR aponta para diretório no disco temporário do runner, evitando depender
de /dev/shm executável. O build usa dois jobs e o guest recebe mem=1024M.

`basic` é deliberadamente mínimo. `lockdep` adiciona PROVE_LOCKING, LOCKDEP,
DEBUG_LOCK_ALLOC, verificações de mutex/spinlock e objetos work. `kasan` é
um perfil independente. Use QEMU x86_64 para KCSAN Strict.

KUnit configura o kernel e o CI compara **todas as opções pedidas** com o .config
resultante, novamente após build. Se uma opção não for suportada, o job falha com
`configuration_incompatible` e registra os valores faltantes em
`config-compatibility.json`. Não há fallback, remoção silenciosa ou
continue-on-error. Um job de sanitizer incompatível não significa que detectou
um bug no Goodix; tampouco significa que o sanitizer passou. Pode deixar o
workflow global vermelho mesmo se basic passar. A compatibilidade será decidida
pelos resultados reais do runner, sem presumir suporte UML a lockdep/KASAN/KCSAN.

## Critério de aprovação

O modo raw de kunit.py 6.12 não interpreta os resultados como testes reais.
Por isso, após executar o kernel:

- O parser upstream precisa aceitar o log bruto.
- O verificador adicional exige uma única execução KTAP, plano raiz 1..1,
  subteste gx-offline-v07, plano 1..25, cada nome/número na ordem declarada,
  nenhum skip/duplicado/resultado inesperado e conclusão positiva da suíte.
- O log completo é examinado também para padrões de falhas, panic, warnings,
  lockdep e diagnósticos de sanitizers, inclusive depois do último teste.
- `status.json` só recebe passed após execução, parse e verificação; a cópia dos
  fontes originais deve permanecer idêntica.

Compilação, nomes no código, exit 0 do runner raw ou um fragmento parcial não
satisfazem o critério. As verificações do parser usam fixtures sintéticos somente
nos testes Python dos helpers; eles não são KTAP real nem resultados do kernel.

## Evidências por artefato

| Arquivo | Significado |
|---|---|
| status.json | Fase, erro/sucesso, tentativa de execução e confirmação final |
| kernel-provenance.json | Pin/HEAD, hashes do runner, Makefile, .config e imagem UML; commit/run do projeto |
| gx-source-sha256.json, integration.patch | Bytes originais copiados e mudanças feitas só no kernel temporário |
| requested.config, kernel.config | Fragmento pedido e configuração efetiva gerada; boot só é comprovado pelo log/runtime |
| config.log, config-compatibility.json | Saída da configuração e incompatibilidades; sem silenciosamente desabilitar checks |
| build.log, object-audit.log | Compilação e auditoria; não são aprovação KUnit |
| exec.log, kernel-runtime.log | Console do runner e log bruto completo do guest, incluindo KTAP e falhas |
| runtime-stream.ktap | Recorte bruto a partir do primeiro cabeçalho KTAP, inclusive se incompleto/falho |
| upstream-parser.log | Resultado do parser oficial |
| gx-offline-v07.ktap, verified-results.json | Produzidos apenas após aprovação estrita; 25 resultados reais |
| artifact-sha256.json | Integridade dos arquivos textuais coletados |
| runner.log, toolchain.log, logs apt | Ambiente hospedado e ferramentas efetivas |

Uma configuração pode existir mesmo se o kernel não tiver iniciado. Consulte
fase/status e o log bruto. A imagem é identificada por hash, mas imagens Linux,
objetos e módulos não são publicados. Os artefatos ficam disponíveis por 30 dias;
baixe-os para preservação de longo prazo. SHA-256 não é assinatura independente.

Upload usa always() para falhas normais de fetch, configuração, build, timeout e
asserções. Perda do runner, cancelamento forçado ou falha do próprio serviço de
artefatos podem impedir a coleta; os logs da página Actions continuam relevantes.

## Investigar resultados

Comece pelo primeiro erro do log completo e pelo perfil. Para os casos de
concorrência, confira especialmente two_workers_and_owned_queue,
cancel_processing, cancel_data_processing, completion_timeout_race,
restart_with_old_worker, queue_overflow e remove_processing. Eles usam barreiras
para provocar interleavings; a execução bem-sucedida cobre os cenários testados,
não prova ausência universal de corridas ou vazamentos. KASAN e KCSAN não são
detectores gerais de vazamento; nenhum detector de leaks adicional foi alegado.

Para reproduzir num ambiente descartável com as mesmas dependências:

```bash
mkdir -p /tmp/gx-ci-evidence
bash ci/goodix/fetch-kernel.sh /tmp/gx-ci-kernel-novo
python3 ci/goodix/run.py --profile basic --kernel /tmp/gx-ci-kernel-novo \
  --source "$PWD/research/offline-kunit" --work /tmp/gx-ci-work-novo \
  --artifacts /tmp/gx-ci-evidence
python3 ci/goodix/finalize.py /tmp/gx-ci-evidence
```

Use diretórios novos e não execute isso no notebook como substituto do runner
hospedado. Configuração rejeitada deve permanecer registrada; nenhum resultado
autoriza integrar hardware ou acrescentar reset/PM/firmware como recuperação.

## Correção da auditoria de built-in (2026-11-03)

O runner audita agora `drivers/misc/gx_offline/built-in.a`. No Linux 6.12,
`real-obj-y` expande os componentes do alvo composto antes de criar o arquivo;
a regra `ld_multi_m` que gera o objeto composto se aplica a módulos. Com `=y`,
portanto, exigir `gx_offline_test.o` era um erro no contrato do auditor.
Kconfig, Makefile e Kbuild Goodix permanecem iguais; nenhum C foi alterado.

Antes da auditoria, o runner exige exatamente um membro para cada um dos seis
objetos esperados, seus arquivos reais no build e os 25 símbolos dos casos na
imagem `linux`, além dos limites da tabela KUnit e do nome da suíte. A auditoria
original recebe o arquivo real e verifica seus membros com nm/readelf. Não há
build separado para satisfazer o auditor. A comprovação funcional permanece a
execução da mesma imagem, parser upstream e verificação estrita dos 25 resultados.

Novos diagnósticos, preservados também após falhas:

- `generated-objects.txt`: todos os .o/.a/.ko existentes e seus caminhos.
- `kbuild-commands.log`: arquivos .cmd Goodix e comandos de vínculo da imagem.
- `compile_commands.json`, `goodix-compile-commands.json`: comandos C completos
  e recorte Goodix, quando gerados pelo KUnit.
- `goodix-archive-members.log`: membros reais de built-in.a.
- `uml-defined-symbols.log`: símbolos definidos na imagem executável.

`KBUILD_VERBOSE=1` solicita comandos detalhados ao make; os .cmd preservam os
comandos mesmo quando kunit.py resume a saída de um build bem-sucedido.
`finalize.py` também coleta os diagnósticos caso o runner seja interrompido.
