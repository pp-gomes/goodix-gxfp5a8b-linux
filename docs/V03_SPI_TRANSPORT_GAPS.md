# Versão funcional 0.3 — lacunas do transporte SPI e bloqueio FF FF

**0.3 permanece não concluída.** O controlador Linux entregou `FF FF` nas
tentativas experimentais relatadas; não foi demonstrada uma resposta válida do
Goodix. A causa física não foi determinada. Este documento analisa somente
fontes e registros existentes, sem novo I/O.

## Evidências e separação dos contratos

| Evidência | Constatação | Alcance |
|---|---|---|
| ZIP histórico “v0.3” | Somente inventário e contador opcional de IRQ | Não implementa transporte SPI |
| Fonte histórico “v0.4”, linhas 99/101 | spi_write com F0 00 A8; nova mensagem spi_write_then_read com F1/RX2 | Sequência host implementada; resultado relatado FF FF |
| Fonte histórico “v0.5”, linhas 107/111/115/120 | E5 bruto opcional, espera 50–55 ms, F0, F1/RX2 | E5 não resolveu o resultado relatado; não há confirmação de recepção pelo MCU |
| Windows | 14 leituras CPU 00 A1 e 17 SelfChecks bem-sucedidos, sem HardResetMcu registrado, segundo histórico fornecido | Referência operacional Windows, sem waveform nem histórico elétrico Linux equivalente |
| Protocolo offline | Construção, parsing, checksums, ACK/dados e concorrência testados | Não exercita SPI físico, alimentação, CS ou sinais GPIO |

Fontes: membros e hashes em [COMPONENT_TRACEABILITY.md](COMPONENT_TRACEABILITY.md),
[relatório offline anterior](../research/offline-mcu/README.md),
[contrato SPI/GPIO](PUBLICATION.md#exclusões) e
[diagnóstico da plataforma](PUBLICATION.md#exclusões).

Há três entidades diferentes a manter separadas:

- **Endereço SPI 0x00A8:** lido por McuGetCpuVersion, com F0 e F1; a referência
  CPU fornecida é 00 A1. Não é por si só uma consulta de Chip ID.
- **Opcode MCU A8:** consulta de versão por pacote/mailbox, com ACK/dados.
  Seus fixtures offline não são uma resposta física ao endereço 0x00A8.
- **Consulta de Chip ID:** pertence à etapa funcional 0.5 e exige contrato próprio;
  o caminho MCU 0x82 discutido nos relatórios não deve ser acionado nesta tarefa.

Os pedidos F0 e F1/RX são separados nos dois sistemas. Dentro da segunda mensagem,
o contrato recuperado prevê continuidade de CS entre TX F1 e RX2. Não foi achada
divergência lógica positiva de fronteira de mensagens que explique FF FF.
Isso não valida o CS elétrico, a frequência aplicada, o pinmux SPI ou os tempos
da pilha real. Não há base para acrescentar dummy bytes ou mudar CS/clock.

## Critérios de aceitação da 0.3

Estes critérios operacionalizam o objetivo original de TX/RX; não antecipam
o fechamento funcional de IRQ, Chip ID, inicialização ou biometria.

| ID | Critério de aceitação | Estado atual / evidência que falta |
|---|---|---|
| A03-01 | Identificar cliente ACPI, controlador e CS corretos por recursos, com isolamento da flash e sem hardcode de bus/GPIO/IRQ | Parcial: identidade histórica conhecida; falta dossiê completo vinculado ao ensaio. A exigência bus0 dos módulos antigos não é identificação portátil |
| A03-02 | Especificar e comprovar as pré-condições de alimentação, sinal de controle, mux, seleção e estado necessário do dispositivo antes do ensaio | Não satisfeito: função/polaridade/domínio elétrico GFPS e estado contemporâneo ao teste não estabelecidos |
| A03-03 | Definir TX/RX, limites, ordem, CS, perfil e tempos com base no contrato recuperado e na implementação do kernel alvo | Parcial: sequência CPU host recuperada; configuração aplicada/waveform e fonte Ubuntu exato ainda não validados; API genérica de pacotes físicos não demonstrada |
| A03-04 | Operação física delimitada, serialização dos requests relacionados, propagação de erro/timeout e ausência de retry/reset implícito | Parcial: diagnósticos têm mutex, retorno de erro e one-shot; falta implementação de transporte funcional revisada. Código offline mantém backend desabilitado |
| A03-05 | Obter **resposta válida no Linux** ao comando definido, com TX/RX bruto, comprimento, retorno do controlador e interpretação verificável | **Não satisfeito e bloqueante:** só há FF FF informado. Exit zero, bytes não constantes ou simples diferença de FF FF não bastam |
| A03-06 | Validar comprimento/formato/endianness e, quando o protocolo tiver, checksums; rejeitar respostas vazias, truncadas ou incompatíveis | Parcial: validações em memória aprovadas; falta aplicá-las a dados reais Linux. Leitura direta CPU não tem o checksum do pacote MCU |
| A03-07 | Preservar e revisar proveniência/repetibilidade: hashes, kernel, parâmetros, pré-condições, sequência e resultado do ensaio, sem depender apenas de relato | Não satisfeito para transporte válido: falta dossiê de uma execução física bem-sucedida e sua confirmação delimitada |

Para o diagnóstico CPU conhecido, 00 A1 seria a referência esperada nas condições
documentadas. Outro valor só poderá ser aceito com interpretação independente
justificada. O primeiro resultado válido será evidência necessária, mas o
fechamento de 0.3 exige também os demais critérios. Não se transforma uma leitura
de dois bytes em comprovação de suporte a todos os pacotes MCU.

## Hipóteses, evidência discriminante e limites

| Hipótese | O que a sustenta / enfraquece | Lacuna a resolver antes de propor intervenção |
|---|---|---|
| MISO sem acionamento, seleção ou alimentação inadequada | FF FF é compatível com linha alta, mas não identifica qual condição ocorreu | Evidência elétrica ou documentação de rails, CS e MISO; ausente nos registros atuais |
| GFPS em estado inadequado | Windows abre GPIO-I/O e solicita zero; Linux não adquire a linha | Snapshot Linux já mostra saída/latch zero; falta função elétrica, polaridade, tensão, ownership/locks completos e correlação ao ensaio |
| Diferença de preparação Windows | Windows passa pela consulta de versão MCU antes da CPU no SelfCheck | Ordem de software não demonstra que A8 habilite a CPU; não enviar A8 como tentativa de correção |
| MCU não recebeu WakeUp | Linux “v0.5” acrescentou E5, mas permaneceu FF FF | Êxito de spi_write não confirma recepção/estado MCU; apenas E5 faltante não explica mais o conjunto de resultados |
| Configuração/CS/clock/pinmux do SPI | Perfil declarado coincide; não há divergência lógica demonstrada | Falta configuração efetiva e evidência física. GPIO46/259 não verifica pads SCK/MOSI/MISO/CS |
| PM/retomada do SSP/LPSS | Autosuspend informado 50 ms coincide com espera pós-E5 | Core de referência retoma para novas mensagens; “v0.4” também falhou sem E5/espera. Falta trace histórico ou diferença concreta no kernel alvo |
| IRQ GFPI ausente | Recepção de pacotes MCU depende de IRQ | Leitura CPU síncrona não aguarda GFPI. IRQ do controlador SPI é outro recurso; contador isolado não resolve FF FF |
| Endianness ou parser | Parser/ordem precisam estar corretos | Trocar FF FF continua FF FF; não explica a diferença para 00 A1 |
| Reset necessário | SelfCheck Windows tem ramo condicional de recuperação | Não houve registro de HardResetMcu nos 17 sucessos informados. Não é requisito demonstrado nem fallback autorizado |

Nenhuma linha dessa tabela é causa confirmada. Uma diferença de software não
é prova de pré-condição elétrica violada. Solicitar GPIO pode modificar mux ou
direção; reproduzir a escrita zero Windows não é uma operação neutra.

## Limites específicos das evidências de plataforma

GFPS: o snapshot 0x84000200 corresponde a GPIO de saída com latch zero; não mede
o fio nem a alimentação. A função reset/enable/power e inversões externas não
foram estabelecidas. SHPO em _INI altera intenção de ownership, não o latch TX.
`active-high` de uma ferramenta GPIO não define a polaridade funcional de GFPS.

GFPI: _CRS altera o template inicial e devolve Edge/ActiveHigh. Usar o template
Level/ActiveLow isoladamente seria incorreto. Pino ACPI 259, pad Intel 186 e IRQ
Linux histórica 176 pertencem a espaços diferentes; não codificar tais números.

PM: 5 s do S0 Idle Windows refere-se ao dispositivo Goodix; 50 ms de autosuspend
Linux refere-se ao controlador pxa2xx, e o pai PCI tem política própria. Não
deduzir falha causal da razão 5000/50. O pacote alvo informado é
linux-hwe-7.0 7.0.0-42.42~24.04.1, cujo diff exato está pendente. O Linux 6.12
fixado no CI offline não certifica essa pilha física.

Referências detalhadas: [contrato GFPS](PUBLICATION.md#exclusões),
[mapeamento](PUBLICATION.md#exclusões),
[lacunas elétricas](PUBLICATION.md#exclusões),
[cronologia PM teórica](PUBLICATION.md#exclusões) e
[diff pendente](PUBLICATION.md#exclusões).

## Condição para retomar o trabalho físico

Concluir primeiro o dossiê e os gates de [NEXT_STEPS.md](NEXT_STEPS.md).
Nesta tarefa não se propõem comandos ao sensor, aquisição GPIO, leitura MMIO,
alteração de PM, IRQ física, reset, firmware, PSK ou OTP. A flash spi1.0 é
excluída de qualquer alvo ou fallback. Os READMEs antigos com comandos ativos
são evidência histórica, não instruções atuais de execução.
