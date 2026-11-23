# Goodix v0.7: KCSAN strict em x86_64/QEMU

## Executar no GitHub

Publique `.github/workflows/goodix-kcsan-qemu.yml`, `ci/goodix/` e os fontes
existentes de `research/offline-kunit/`. Em Actions selecione **Goodix v0.7 KCSAN
x86_64 QEMU → Run workflow**, escolhendo a branch que contém esses arquivos.
O workflow é independente do UML: não depende de seus jobs e não os altera.

Em uma máquina com gh autenticado ao repositório, alternativamente:

```bash
gh workflow run goodix-kcsan-qemu.yml --ref SUA_BRANCH
gh run list --workflow goodix-kcsan-qemu.yml
gh run view RUN_ID --log
gh run download RUN_ID --name goodix-kcsan-qemu-RUN_ID-ATTEMPT --dir evidencias-kcsan
```

Esses comandos não foram executados nesta sessão. Não execute o runner de teste
no LG Gram. Ele foi preparado para GitHub-hosted **ubuntu-24.04 x86_64**.

## Kernel, instrumentação e execução

`kernel.json` e `fetch-kernel.sh` são reutilizados sem alteração: Linux v6.12,
commit `adc218676eef25575469234709c2d87185ca223a`. O runner exige HEAD exato e
árvore limpa antes de copiar os mesmos arquivos Goodix. Registra hashes de
origem, compara os bytes copiados e preserva o patch de integração temporária.
Os C, headers, fixtures, Kconfig e Kbuild do projeto não são editados.

`profiles/kcsan-x86_64.config` solicita KCSAN e KCSAN_STRICT, verifica suporte
arquitetural e do compilador, e mantém o selftest de boot do KCSAN. KASAN e a
suíte KUnit interna do KCSAN ficam desabilitados: a suíte selecionada para esta
execução é `gx-offline-v07`. Todas as opções pedidas são comparadas com o
`.config` efetivo antes e depois da compilação, sem fallback.

A configuração solicita SMP com NR_CPUS=2 e suporte APIC/ACPI para o guest.
QEMU usa **TCG com duas vCPUs**, `-cpu max` e 2 GiB de RAM. Não depende de KVM.
Não há dispositivos do host repassados, discos, diretórios compartilhados,
backend físico Goodix ou rede configurada. APIC, ACPI e serial são virtuais.

O KUnit upstream gera a configuração x86_64. O make compila `bzImage` e o
banco de comandos com `V=1`, registrando stdout e stderr completos. O runner
compara a configuração extraída desse bzImage com o `.config` verificado e
registra SHA-256 da imagem e do vmlinux.

Antes do boot, exige:

- vmlinux ELF64 de arquitetura x86_64;
- os seis membros reais de `drivers/misc/gx_offline/built-in.a`;
- os 25 símbolos dos casos e a tabela KUnit em vmlinux;
- `-fsanitize=thread` nos comandos dos seis C;
- referências a hooks de acesso à memória em gx_exchange.o e gx_kunit.o;
- runtime KCSAN vinculado à imagem;
- auditoria original de imports físicos, aliases e parâmetros aprovada.

Executa QEMU diretamente, com a imagem auditada, console serial e filtro KUnit
`gx-offline-v07`. O argumento `kcsan.early_enable=1` ativa o detector antes dos
testes. O guest reinicia após o KUnit; `-no-reboot` encerra QEMU nesse ponto.
Há timeout de configuração de 600 s, build de 3600 s e QEMU de 600 s, com término
do grupo de processos. O job tem limite de 120 minutos.

O console precisa confirmar KCSAN ativo cedo, modo strict, a versão de teste
`6.12*-gx-v07-kcsan` e duas CPUs online. Depois, o parser KUnit upstream e o
verificador independente existente exigem os 25 casos na ordem esperada, sem
falha, skip ou diagnóstico no log completo. O código de saída QEMU, isoladamente,
não aprova o perfil. Ausência de reports não prova ausência universal de corridas:
KCSAN usa amostragem, e esta execução cobre os cenários exercitados.

## Artefatos e interpretação

Baixe `goodix-kcsan-qemu-<run_id>-<attempt>`, retido por 30 dias.

| Evidência | Conteúdo |
|---|---|
| status.json | Status, fase, tipo de falha e confirmação final de execução |
| kernel-provenance.json | Commit fixo, ambiente CI, hashes de config, vmlinux e bzImage |
| gx-source-sha256.json, integration.patch | Fontes integrados e alterações só no kernel temporário |
| requested.config, kernel.config, config-compatibility.json | Configuração solicitada, efetiva e divergências |
| boot-image.config, image-config-extraction.log | Configuração extraída da imagem de boot e erros de extração |
| build.log, generated-objects.txt | Compilação verbosa e inventário real |
| compile_commands.json, goodix-compile-commands.json | Comandos C completos e recorte Goodix |
| kbuild-commands.log, boot-build-commands.log | Comandos salvos pelo Kbuild, incluindo criação da imagem |
| goodix-archive-members.log, object-audit.log | Membros reais e auditoria de hardware |
| vmlinux-elf-header.log, vmlinux-defined-symbols.log | Arquitetura e símbolos vinculados |
| instrumentation-imports.json, instrumentation-audit.log | Hooks nos objetos e confirmação de instrumentação |
| qemu-command.json, qemu.log | Argumentos exatos e console completo, inclusive stderr QEMU |
| kernel-runtime.log, kcsan-diagnostics.log | Cópia do console e índice de linhas relevantes; stacks completos em qemu.log |
| runtime-instrumentation.json | Confirmações de boot do KCSAN strict e SMP |
| runtime-stream.ktap | Recorte bruto, inclusive incompleto ou falho |
| upstream-parser.log | Resultado do parser oficial do kernel fixado |
| gx-offline-v07.ktap, verified-results.json | Resultados produzidos após aprovação estrita dos parsers |
| helper-tests.log, runner.log, toolchain.log, logs apt/fetch | Checks dos auxiliares e ambiente |
| artifact-sha256.json | Manifesto SHA-256 dos arquivos textuais |

Somente `status=passed` e `kernel_executed_and_verified=true`, juntamente com
25 pass, zero fail e zero skip em verified-results.json, aprovam o perfil.

`configuration_incompatible` é incompatibilidade de configuração. As fases
`build`, `image_config_audit`, `object_audit` e `execute` identificam problemas
de build, integração/instrumentação ou execução QEMU. Nenhuma delas, sozinha,
é evidência de corrida Goodix. Reports durante boot/testes impedem aprovação,
mas precisam ter suas stacks analisadas antes de atribuição ao Goodix.
Falhas em `runtime_instrumentation` também podem indicar evidência de boot
incompleta; consulte o erro registrado e o console integral.

Coleta e upload usam `always()` e o runner possui `finally`. Timeout, boot
incompleto ou teste falho preservam o console disponível, config e comandos.
Cancelamento forçado, perda do runner ou falha do serviço de artefatos podem
impedir upload. Imagens e objetos não são publicados; são identificados pelos
hashes e pelos diagnósticos textuais.

## Verificações locais dos auxiliares

```bash
python3 -m unittest discover -s ci/goodix/tests -v
```

Os dados de boot e KTAP nesses testes são **fixtures sintéticas**. Não representam
um resultado KCSAN/QEMU real. Consulte `v07-kcsan-qemu-validation-report.md` para
o estado da validação entregue.
