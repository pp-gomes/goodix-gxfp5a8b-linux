# Próximos passos — concluir o transporte funcional 0.3

O próximo objetivo é obter e demonstrar TX/RX SPI válido no Linux, conforme
[ROADMAP.md](ROADMAP.md). O bloqueio atual é FF FF, não uma insuficiência dos
testes offline. Os componentes já validados permanecem disponíveis para regressão.

**Escopo desta entrega: análise e reorganização documental.** Os passos 1–5
abaixo usam somente arquivos já coletados. O passo 6 descreve uma etapa futura,
condicionada e não executável a partir deste documento. Não são fornecidos
comandos de acesso ao notebook nem solicitada execução de hardware agora.

## 1. Fechar o índice de evidências históricas

Reunir, sem alterar os originais, os registros já existentes de bind 0.1,
descritores 0.2, tentativas FF FF, logs Windows e resultados dos quatro perfis.
Para cada registro: origem, hash, fonte/binário, kernel, boot/sessão, timestamps
e o que foi efetivamente observado. Campos ausentes ficam “não disponível”.
Uma informação transcrita mantém sua classificação de relato; não fabricar log.

Priorizar os logs históricos das tentativas Linux. Os [quatro perfis offline](evidence/README.md)
já têm registros validados para os mesmos 25 casos da suíte gx-offline-v07,
incluindo KCSAN Strict na execução QEMU 41592466405.

**Produto:** índice por execução que permita distinguir evidência local, relato
e inferência. **Gate:** nenhuma declaração de fechamento 0.1/0.2 ou de transporte
será ampliada além do que esse índice sustenta. Não atrasar a análise dos
arquivos disponíveis por ausência de um log.

## 2. Especificar as pré-condições elétricas e de identidade

Usar DSDT, mapeamento de recursos, exports Windows e snapshots já arquivados
para ligar FPNT/GXFP5A8B à conexão SPI, GFPS e GFPI. Revisar recurso final _CRS,
efeitos condicionais de _INI/SHPO, e separar recursos declarados de estado
aplicado no ensaio. Não executar AML novamente.

Especificar função de GFPS, polaridade funcional, domínio de tensão, possíveis
inversões, estado requerido do componente e comportamento de aquisição/direção.
Usar documentação elétrica da placa/componente ou capturas históricas que já
existam. Latch zero, ausência de consumidor e escrita zero Windows não preenchem
esses campos. Registrar PAD_OWN, HOSTSW_OWN e locks somente se houver evidência
bruta suficiente; não inferir um do outro.

**Produto:** contrato de pré-condições para A03-01/A03-02, com origem de cada
campo. **Gate:** se função elétrica, alimentação ou segurança da transação não
puderem ser estabelecidas, manter o ensaio físico bloqueado. A próxima aquisição
é documental em outra estação; não requisitar GPIO para descobrir sua função.

A ficha LG Brasil confirma o modelo `14Z90N-V.BR51P1` e a presença do leitor.
O relatório de certificação da série 14Z90N mostra o módulo e seu cabo
flexível nas figuras 49 e 50. O catálogo LG Parts identifica
`EBD63285705` como leitor, e a lista de um distribuidor o associa a
outras variantes 14Z90N. A montagem da variante brasileira e a pinagem
do seu conector continuam por confirmar.
[LG Brasil](https://www.lg.com/br/computadores/notebook-lg-gram/14z90n-v-p2/) ·
[Fotos de certificação](https://fccid.io/m/ca8310bfba872274321d0bec12768f22a59fa3f35fb706fcd79dc5e6fc82b9a5.pdf) ·
[LG Parts](https://lgparts.com/pt/collections/laptop/products/ebd63285705) ·
[Lista do distribuidor](https://www.spareslg.com/gb/sensor-fingerprint-para-portatil-lg-ebd63285705.html)

A próxima fonte independente é a lista de peças LG para o sufixo exato,
seguida pelo esquema do conector na revisão confirmada da placa e pela
especificação elétrica do módulo. Esses documentos precisam identificar
o destino da rede GFPS, eventuais inversores, as alimentações, a tensão
de I/O e a condição de disponibilidade para SPI. Para futura revisão
especializada, uma captura passiva de uma inicialização funcional deve
correlacionar o sinal no lado do módulo e suas alimentações com o CS e
a primeira leitura válida.

## 3. Conferir contrato SPI e pilha do kernel alvo

Revisar lado a lado os fontes preservados “v0.4/v0.5” e a cadeia Windows já
exportada: F0 separado; F1+RX na mesma sequência; perfil, CS e comprimentos;
proteção do seletor e erros em cada fase. Distinguir endereço CPU 0x00A8,
opcode MCU A8 e Chip ID. Identificar o comando mínimo cuja segurança e resposta
tenham base independente; não assumir que a ordem Windows seja uma dependência
elétrica obrigatória ou que A8 resolva FF FF.

A [comparação autenticada Ubuntu -42/-43](../src/README.md#comparação-dos-fontes-ubuntu--42-e--43)
cobriu 26 arquivos, com igualdade byte a byte. A [auditoria focal do CS
nativo](V03_SPI_TRANSPORT_GAPS.md#cs-nativo-e-modos-históricos) rastreia o
perfil ativo baixo até o pxa2xx. Falta evidência do nível físico de CS
durante as mensagens e do estado inicial do sensor.

O [controlador Intel instalado](V03_SPI_TRANSPORT_GAPS.md#controlador-intel-spi-no-windows)
já foi identificado e analisado estaticamente. Se houver registros
arquivados do controlador, correlacionar as transições de energia e os
parâmetros efetivos de clock e CS com a primeira solicitação SPI de cada
ensaio. Sem esses registros, priorizar a documentação elétrica da revisão
da placa e a avaliação especializada descritas na seção 2.

**Produto:** contrato TX/RX de A03-03 e mapa dos caminhos de erro A03-04.
**Gate:** nenhuma mudança de CS, divisor, dummy byte, espera ou política PM por
tentativa. Só uma diferença demonstrada poderá fundamentar proposta posterior.

## 4. Construir uma decisão verificável sobre FF FF

Para cada hipótese de [V03_SPI_TRANSPORT_GAPS.md](V03_SPI_TRANSPORT_GAPS.md),
registrar: fato que a sustenta, fato que a limita, dado discriminante ainda
ausente e se há fonte histórica capaz de fornecê-lo. Correlacionar pelo mesmo
boot/ensaio; snapshot posterior não reconstrói o estado anterior.

Se a distinção restante depender de sinal elétrico, escrever uma especificação
de observação futura para revisão especializada, identificando sinal, propósito,
segurança elétrica e resultado discriminante. Não recomendar nova leitura de
registradores, gpioinfo/gpioget/debugfs ou carga de módulo como forma “passiva”
de preencher a lacuna. A especificação não autoriza medição ou nova transação.

**Produto:** decisão de prontidão ou lista objetiva de bloqueadores. **Gate:**
ausência de causa confirmada permanece explicitada; não escolher reset, PM ou
GFPS apenas porque estão disponíveis no software.

## 5. Preparar o contrato de um futuro transporte mínimo

O [núcleo C e o adaptador Linux desabilitado](../src/spi/README.md) já
implementam o contrato do diagnóstico de CPU. A regressão em host usa
`make -C tests/spi test sanitize`. Para revisar uma integração futura,
aplicar os requisitos abaixo:

- identidade por recursos ACPI e cliente correspondente, com exclusão de spi1.0;
- operação única e explicitamente delimitada, sem I/O implícito em probe;
- limites de TX/RX, resultado bruto, tamanho e retorno por fase;
- serialização do seletor entre requests do mesmo sensor, sem segurar lock global
  do barramento durante esperas de IRQ;
- falha explícita, sem loop de retries, reset, wakeup adicional ou ajuste PM
  automático para compensar erro;
- separação entre falha de host, timeout e resposta semanticamente inválida;
- instrumentação de logs que preserve o contexto sem enviar comandos extras.

As salvaguardas dos diagnósticos antigos são referências, não módulos a carregar
agora. A fonte sintética e gx_transport_disabled permanecem intactos. Planejar
como testar o futuro contrato usando fixtures e a regressão já aprovada, sem
afrouxar a auditoria de ausência de hardware dos componentes offline.

**Produto:** especificação revisável e plano de evidência por A03-01–A03-07.
**Gate:** o plano deve apontar exatamente o que uma eventual resposta comprovará;
um diagnóstico de CPU não certificará automaticamente pacote MCU, IRQ ou Chip ID.

## 6. Etapa futura condicionada: ensaio físico mínimo

Só considerar após resolver as pré-condições, revisar o contrato e obter escopo
específico para a intervenção. Se ainda for necessária ação GPIO, reset ou PM,
ela exige justificativa e revisão própria; não estará implicitamente autorizada
por copiar a preparação Windows. Esta tarefa não executa essa etapa.

O eventual ensaio deve ter alvo Goodix inequívoco, ambiente e código identificados,
condições iniciais documentadas, sequência delimitada, critério de parada e
logs de TX/RX/status. Não combinar sondagem de IRQ, comandos MCU, reset ou
provisionamento como tentativas de recuperação. Qualquer proposta de instrumentação
elétrica depende de revisão competente de ligação e níveis, sem inferi-los do
nome de mux ou número GPIO. spi1.0 nunca integra o ensaio.

**Evidência necessária:** resposta Linux consistente com o contrato do comando,
por exemplo CPU 00 A1 se esse diagnóstico for o escolhido e justificado, além
de comprimento, retorno por fase e pré-condições. FF FF, sucesso do host, contador
de IRQ ou bytes diferentes porém sem significado não satisfazem A03-05.

Uma primeira resposta válida abre a revisão de aceitação; não fecha sozinha
0.3. Planejar confirmação delimitada/reprodutível, sem repetição automática,
e verificar os demais critérios, inclusive o alcance real de leitura/escrita
de pacotes. Se a resposta permanecer inválida, parar e revisar a evidência;
não escalar automaticamente para GPIO, firmware, PSK ou OTP.

## 7. Encerrar 0.3 e só então retomar dependências

Atualizar CURRENT_STATUS apenas quando A03-01–A03-07 estiverem satisfeitos e
vinculados às evidências revisadas. Manter juntos resultados físicos e regressões
offline, distinguindo seus alcances e limitações.

Depois da aceitação, retomar 0.4: IRQ GFPI real, leitura de mailbox e entrega de
respostas MCU, reaproveitando parsers, staging e contratos de concorrência.
O contador antigo não substitui essa implementação. Seguir então o roadmap
0.5 → 0.6 → 0.7 → 1.0. Não desenvolver 0.8, protocolo biométrico ou libfprint
enquanto os pré-requisitos correspondentes estiverem abertos.
