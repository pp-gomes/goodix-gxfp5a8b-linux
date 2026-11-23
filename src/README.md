# Transporte SPI 0.3 — diagnóstico offline

Este modelo Python usa somente a biblioteca padrão. As três partes são o
contrato de mensagem e identidade em [transport.py](transport.py), a sequência
da CPU e o estado da sessão em [diagnostic.py](diagnostic.py), e o backend
em memória em [simulated.py](simulated.py).

Na raiz do projeto, execute:

```sh
python3 -B -m unittest discover -s tests -p test_diagnostic.py -v
```

## Contrato

| Mensagem | Transferências dentro de uma janela de CS | Comprimento total |
|---|---|---|
| Seleção do endereço `0x00A8` | TX `F0 00 A8` | 3 |
| Leitura | TX `F1`, depois RX de dois bytes com CS contínuo | 3 |

O fim de cada mensagem encerra sua janela de CS. Cada recibo contém status
do backend, comprimento efetivo total e bytes RX confirmados. Um erro na
seleção termina a operação antes da leitura. Com status zero e comprimentos
corretos, `00 A1` corresponde à referência documentada e `FF FF` recebe
`INVALID_RESPONSE`, preservando status zero e os bytes recebidos. Valores
diferentes da referência têm significado ainda indeterminado neste diagnóstico.
O opcode MCU `A8` e a consulta de Chip ID têm contratos próprios.

Uma sessão serializa o par para seu cliente. O prazo monotônico começa
na admissão, após a espera pela vez, e é compartilhado pelas duas mensagens.
O cancelamento sinaliza a operação ativa. A remoção fecha a admissão,
sinaliza a operação ativa e espera seu retorno. Cancelamento e prazo são
cooperativos. Uma transferência já iniciada pode terminar e deixar um recibo.
Se a remoção ou cancelamento vencer a publicação do resultado, a sessão
reporta esse estado e conserva os recibos existentes. O backend deve devolver
o controle com o uso dos buffers encerrado. Chamadas de controle da sessão
devem vir de fora do callback do backend, que executa sob serialização.

## Integração física futura

O adaptador Linux deve trazer o contrato para C e vincular uma sessão por
cliente à instância SPI já associada ao recurso ACPI. Deve verificar
HID/caminho do sensor, origem ACPI do recurso, vínculo com o controlador PCI
e perfil aplicado. A instância deve vir do bind inequívoco ao Goodix.
`spi1.0` é a flash e fica permanentemente excluído. O número global do
barramento fica fora da identificação do sensor.

Após auditoria da pilha Ubuntu, mapear cada mensagem lógica para uma
`spi_message` e submeter separadamente com `spi_sync`. A segunda contém dois
`spi_transfer`, com `cs_change` desativado e buffers adequados ao controlador.
Esse mapeamento permite conservar status e `actual_length` por mensagem.
Confirmar o contrato de buffers e dados parciais no controlador antes de
reportar RX parcial como observado. O adaptador precisa tratar o retorno real
do controlador e drenar as operações em remoção antes de liberar dispositivo ou
buffers. Um prazo lógico não interrompe um `spi_sync` em andamento. O projeto
do adaptador deve se apoiar nos timeouts e na conclusão que a pilha alvo de
fato oferece. A serialização do par pertence ao cliente.

## Pré-condições para o primeiro ensaio

- Confirmar a identidade pelo recurso ACPI e perfil do cliente Goodix no
  ambiente autorizado, com exclusão da flash.
- Estabelecer alimentação, função, polaridade e domínio elétrico de GFPS,
  estado inicial do sensor, mux e propriedade dos pads. O latch arquivado
  sozinho não determina essas condições.
- Auditar o SPI core e as dependências PM da revisão alvo e revisar o adaptador,
  os limites, o tratamento de erro e o ciclo de vida.
- Justificar a segurança da leitura mínima no estado inicial escolhido.
  Qualquer necessidade de preparar reset, alimentação, GPIO ou PM exige
  evidência e autorização própria.
- Revisar um plano explícito com autorização, critério de parada, registro
  de kernel/código/perfil e status/comprimento/RX por fase. Caso seja preciso
  distinguir clock/seleção ausentes de MISO alto durante o tráfego, revisar
  também a instrumentação elétrica e os níveis antes de qualquer medição.

A referência simulada cobre o contrato do diagnóstico. O critério de primeira
resposta válida em 0.3 continua dependendo de evidência obtida no Goodix físico.

## Comparação dos fontes Ubuntu -42 e -43

Os pacotes completos `linux-hwe-7.0` das revisões `7.0.0-42.42~24.04.1` e
`7.0.0-43.43~24.04.1` foram extraídos com seus patches aplicados fora do
repositório. A assinatura de cada `InRelease` foi verificada com a chave
Ubuntu Archive Automatic Signing Key (2018). Os SHA-256 dos índices
`Sources.xz` conferem com os `InRelease`. Os tamanhos e SHA-256 dos
descritores, patches e tarball conferem com os índices autenticados.
[Snapshot noble-security -42](https://snapshot.ubuntu.com/ubuntu/20261104T000000Z/dists/noble-security/) ·
[Snapshot noble-updates -43](https://snapshot.ubuntu.com/ubuntu/20261119T000000Z/dists/noble-updates/) ·
[Pool Ubuntu](https://archive.ubuntu.com/ubuntu/pool/main/l/linux-hwe-7.0/)

| Artefato | SHA-256 verificado |
|---|---|
| `linux-hwe-7.0_7.0.0.orig.tar.gz` | `c6343795c8c7e4feff55f5b309365c8e0eaf49cd6abcb412e32315313f149536` |
| `linux-hwe-7.0_7.0.0-42.42~24.04.1.diff.gz` | `ab5b2722d3d4bd00a97f4f048f1dc2d8a50cdbbc7ed868aac4ffc59bb516b076` |
| `linux-hwe-7.0_7.0.0-43.43~24.04.1.diff.gz` | `f377aaedcdc3fc17aab4e453a63c9f34447b0d1081707bd7d137dd5a845df794` |

A comparação retornou igualdade byte a byte em 26 arquivos que cobrem SPI
core, pxa2xx (platform, PCI e DMA), Intel LPSS, runtime PM e os caminhos
auxiliares ACPI/PCI e cabeçalhos revisados. As anotações de configuração
comparadas tampouco mostraram alteração nas opções desses subsistemas.
O resultado cobre o fonte e as opções examinadas. O estado efetivo no
notebook e a causa de `FF FF` continuam desconhecidos.

- O SPI core resolve o controlador pela origem do recurso ACPI e extrai CS,
  modo, frequência e tamanho da palavra. O ID PCI `8086:34ab` seleciona
  `LPSS_CNL_SSP`, para o qual pxa2xx conserva o número de CS do ACPI.
  A integração futura deve receber o cliente enumerado pelo SPI core.
- `spi_sync` aguarda a conclusão. O core mantém CS entre transfers com
  `cs_change` e `cs_off` zerados e libera CS ao final. No caminho pxa2xx,
  `actual_length` soma transfers concluídos. `spi_write_then_read` copia
  o RX quando o status é zero e não fornece essa contagem ao chamador.
- O core toma a referência de runtime PM antes de iniciar uma fila ociosa
  e propaga falhas de retomada. pxa2xx reativa o clock na retomada e o pai
  LPSS restaura o contexto. A espera de conclusão de transfer em modo
  controlador no core usa o tempo de fio dobrado mais 200 ms. O prazo
  lógico e o cancelamento da sessão continuam cooperativos.

A auditoria fecha a comparação de fonte para a transação host. Para
propor o ensaio ainda é preciso comprovar alimentação e estado inicial
seguro do Goodix, a função e a polaridade de GFPS e a identidade do cliente
no ambiente previsto. A resposta do sensor no Linux será avaliada durante
o ensaio autorizado.
