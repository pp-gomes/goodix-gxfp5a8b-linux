# Rastreabilidade de componentes e marcos funcionais

Os números funcionais são os de [ROADMAP.md](ROADMAP.md). Os identificadores
abaixo são apenas referências desta matriz, sem alteração de arquivos, APIs,
suítes KTAP ou versões embutidas. Caminhos originais e evidências são preservados.

## Correspondência

| ID / componente existente | Localização preservada | Capacidade efetiva | Marcos aos quais poderá contribuir | Limite para reaproveitamento |
|---|---|---|---|---|
| C01 — sonda passiva | [ZIP passivo](../historical/passivo/Goodix_GXFP5A8B/gxfp5a8b_probe.c), membro Goodix_GXFP5A8B/gxfp5a8b_probe.c | Match ACPI/SPI e metadados do cliente | 0.1; identidade para 0.2/0.3 | Não envia SPI e não comprova resposta do sensor |
| C02 — inventário ACPI | [ZIP “v0.2”](../historical/v0.2/gxfp5a8b_probe.c), membro gxfp5a8b_probe.c | Enumera _CRS, SPI/GpioIo/GpioInt | 0.2; pré-condições de 0.3/0.4 | Avaliar _CRS executa AML; não é nova coleta autorizada nem validação elétrica |
| C03 — contador IRQ | [ZIP “v0.3”](../historical/v0.3/Goodix_GXFP5A8B_v03/gxfp5a8b_probe.c), membro Goodix_GXFP5A8B_v03/gxfp5a8b_probe.c | Handler opcional conta eventos; modo padrão sem IRQ | Inspeção 0.2; diagnóstico futuro 0.4 | Nenhuma transferência SPI; não cumpre 0.3; contar eventos não recebe respostas MCU |
| C04 — leitura CPU experimental | [ZIP “v0.4”](../historical/v0.4/Goodix_GXFP5A8B_v04/gxfp5a8b_probe.c), membro Goodix_GXFP5A8B_v04/gxfp5a8b_probe.c | F0 00 A8, depois F1/RX2, modo diagnóstico one-shot | Investigação 0.3; referência limitada para 0.5 | FF FF no Linux; não cumpre 0.3/0.4/0.5. Gate bus0 é histórico, não identidade portátil |
| C05 — E5 e leitura CPU experimental | [ZIP “v0.5”](../historical/v0.5/Goodix_GXFP5A8B_v05/gxfp5a8b_probe.c), membro Goodix_GXFP5A8B_v05/gxfp5a8b_probe.c | Acrescenta E5 bruto e espera solicitada 50–55 ms | Investigação 0.3 e sequenciamento futuro 0.6 | FF FF persistiu; E5 recebido/MCU acordado não comprovados. Atributos cpu_version/wakeup_cpu_version fazem I/O ao ler |
| C06 — núcleo MCU offline | [linux/v0.6](../research/offline-mcu/README.md): include/goodix_protocol.h, src/goodix_protocol.c | Construção A8, checksums, ACK/dados, staging/recepção simulada | Formatos para 0.3; parsing 0.4; consultas/inicialização 0.5/0.6; base estrutural futura 0.7 | Não é inicialização funcional 0.6; sem backend e sem prova de semântica biométrica. APIs originais dependem de serialização pelo chamador |
| C07 — núcleo kernel puro | [gx_protocol_core.c](../research/offline-kunit/gx_protocol_core.c), headers; cópia histórica linux/v0.7 | Formatação e parsing derivados de C06 | 0.3/0.4/0.5/0.6 | Formatos em memória não garantem TX/RX elétrico; extensão biométrica exige investigação própria |
| C08 — estado e concorrência | [gx_exchange.c](../research/offline-kunit/gx_exchange.c), [gx_rx_staging.c](../research/offline-kunit/gx_rx_staging.c) | Mutex, preparação/commit, geração, cancelamento, deadlines e buffers | Integração futura 0.3/0.4/0.5/0.6 | Geração local não aparece no fio; staging não tem lock interno; não é driver de interrupção |
| C09 — fonte de eventos sintéticos | [gx_test_event_source.c](../research/offline-kunit/gx_test_event_source.c) | Fila, workqueue, dois workers e cenários de ciclo de vida | Testes de integração futura 0.4/0.6 | Não substituir por ISR real sem projeto de drenagem, erros e lifetime; relógio/deadlines dos testes são sintéticos |
| C10 — transporte desabilitado | [gx_transport_disabled.c](../research/offline-kunit/gx_transport_disabled.c) | Retorno incondicional GX_HARDWARE_DISABLED | Fronteira segura para futura 0.3 | Não é transporte SPI; não ativar por flag para aproveitar nomes de versão |
| C11 — regressão offline | [gx_kunit.c](../research/offline-kunit/gx_kunit.c), tests/fixtures.h; linux/v0.6/tests/test_protocol.c; tools/check-protocol-parity.py | 25 casos KUnit e testes/paridade de protocolo | Regressão transversal de 0.3–0.7, quando aplicável | Fixtures Windows são bytes preservados, não nova resposta Linux; KUnit não valida sensores |
| C12 — CI e auditorias | [.github/workflows/goodix-kunit.yml](../.github/workflows/goodix-kunit.yml), [KCSAN QEMU](../.github/workflows/goodix-kcsan-qemu.yml), ci/goodix/; tools/audit-offline.py | Kernel fixo, inventário Kbuild, auditoria de símbolos, parsers e logs | Regressão dos componentes que contribuírem aos marcos | Manter suíte offline e ausência de hardware; um backend futuro precisará de critérios próprios, sem enfraquecer a auditoria offline |
| C13 — evidência Windows/ACPI/GPIO | [dsdt.dsl](PUBLICATION.md#exclusões), [analysis](PUBLICATION.md#exclusões), driver/Wbdi.dll, relatórios de plataforma | Contratos estáticos, recursos e registros históricos | 0.1–0.6; pesquisa posterior para 0.7 | Software Windows não determina sozinho polaridade, tensão ou estado elétrico Linux |
| C14 — metadados e checks passivos | [linux-readonly-checks.sh](../tests/support/linux-readonly-checks.sh), tests/test_linux_readonly_checks.py | Coletor allowlist e testes com árvores artificiais | Dossiê de identidade e ambiente 0.1/0.2/0.3 | Não executado nesta tarefa; não mede waveform/rails. Não confundir script passivo com atributos antigos que enviam SPI |
| C15 — pacotes e evidências | ZIPs linux/, arquivos de validação, ci/goodix/local-validation/, goodix-uml-basic-41575738572-1.zip, goodix-v07-kcsan-qemu-ci.zip e sua árvore extraída | Histórico de fontes/build/CI e distribuição | Proveniência de todos os marcos | Pacotes e extrações são cópias, não implementações adicionais. Falha CI antiga não é evidência do sucesso posterior informado |
| C16 — transporte diagnóstico 0.3-A | [src/spi](../src/spi/README.md), [tests/spi](../tests/spi/Makefile), [CI offline](../.github/workflows/offline-host.yml) | Sessão C, executor sintético e adaptador Linux desabilitado | Evidência offline para A03-01, A03-03, A03-04 e A03-06 | Reproduzir com `make -C tests/spi test sanitize`. Falta confirmar a origem ACPI do controlador e obter evidência física |

Os relatórios `v07-*` são registros do componente offline, mesmo quando o nome
não o explicita. Os [artefatos KUnit](evidence/README.md) registram os 25 casos
aprovados em cada um dos quatro perfis.

## Critérios vinculados ao transporte

| Critério em V03_SPI_TRANSPORT_GAPS | Componentes de suporte | O que falta além deles |
|---|---|---|
| A03-01 — identidade | C01, C02, C13, C14, C16 | Dossiê por execução, sem número global fixo |
| A03-02 — pré-condições | C02, C13 | Contrato elétrico e estado aplicável ao ensaio |
| A03-03 — contrato TX/RX | C04, C05, C06, C07, C16 | Configuração aplicada, CS/tempos e caminho físico validado |
| A03-04 — operação delimitada e erros | C04, C05, C08, C10, C16 | Implementação física revisada, distinta do backend desabilitado |
| A03-05 — resposta válida | Referência Windows C13; parsers C06/C07 | Resposta real Linux ausente; requisito bloqueante |
| A03-06 — formato e limites | C06, C07, C08, C11, C16 | Associação dos bytes reais ao contrato do comando usado |
| A03-07 — repetibilidade e proveniência | C12, C15 | Dossiê do ensaio físico e validação independente dos resultados |

## Integridade dos arquivos originais

Os ZIPs no acervo original continuam sendo os recipientes dos fontes antigos.
O [manifesto de publicação](PROVENANCE.json) registra os bytes recuperados e as
adaptações desta cópia.

| Arquivo preservado | SHA-256 |
|---|---|
| linux/Goodix_GXFP5A8B_driver_passivo.zip | `10b29fd04c587b3ca3b805fd77f3322c0ddee66a1f1ab23120710852a2470e61` |
| linux/gxfp5a8b_acpi_probe_v02.zip | `7e3c1f7000c0b7d3db0f311a818776ebaddd4e4b918b060c0906fce7d5bdb984` |
| linux/Goodix_GXFP5A8B_probe_v03.zip | `e1277bf39692f54181f8c64afe8ed734a9b030f39ce24f0eb9dd53609199e45b` |
| linux/GXFP5A8B_v04_corrigido.zip | `af4887dab6206ce184522b361de47a5662195e26c000ac2d8937f5b5ef9302e3` |
| linux/GXFP5A8B_v05_E5_cpu_diagnostico.zip | `4e9154dd3352d27486e0453b4f80002c994253c98c03a267cd2928353d1cb08f` |
| linux/GXFP5A8B_v06_offline.zip | `b729d3155591864c70169fce237216da3aaf6f3b42b014576e2e1be77be41b33` |
| linux/GXFP5A8B_v07_source_validation.zip | `f272db7170fcceed8aa31925daf215d3337430c4235bc11c020c982c8116a50b` |
| linux/v0.6.zip | `7b7a6736ab16e5799d0727a138319186d10a627a1f9298a6b2f74d8bc1953d76` |
| linux/v0.7.zip | `3c71a4860208e421bc6713c6c2a53c506771da822d63c4eb1cad107a66d0aef1` |
| linux/v0.7-validation.zip | `a32bfc32d7d071b79057b575f0b967e4081636d98378e81c47c85427f05d1535` |

Os membros C relidos diretamente dos ZIPs também ficam identificados:

| Componente / membro C | SHA-256 dos bytes do membro |
|---|---|
| C01: Goodix_GXFP5A8B/gxfp5a8b_probe.c | `500661905a24a6e13fb303d0a623071933d9f66e0cac0f5a7b87037ed79d84b8` |
| C02: gxfp5a8b_probe.c | `80da87cc36faa4174600355c58b1cf18662f75de9f297cb285c1ac7c8e77df13` |
| C03: Goodix_GXFP5A8B_v03/gxfp5a8b_probe.c | `01cd4e3a5e90d84745a524b15b37bd6933704a9ed558d8e4564edfbbe2e0f9ca` |
| C04: Goodix_GXFP5A8B_v04/gxfp5a8b_probe.c | `aad7926a4e6cb341dfb5ce2f450c2b3def529591d9eea2a639ad7e12bbd03efc` |
| C05: Goodix_GXFP5A8B_v05/gxfp5a8b_probe.c | `ba60d2cb88ccf8d5464b2af19c4a3746a0c5e83367ccab6984d7766a609f7d20` |

Consulte a [revisão de publicação](PUBLICATION.md) para os materiais mantidos apenas no acervo original.
