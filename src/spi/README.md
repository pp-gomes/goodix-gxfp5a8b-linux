# Transporte SPI 0.3 em C

`gx_spi_transport` implementa o diagnóstico síncrono de versão de CPU. Uma
sessão por cliente serializa `F0 00 A8` e, numa segunda janela de CS,
`F1` seguido da recepção de dois bytes. O modelo reutiliza o contrato
Python de [src](../README.md) e a ordem dos fontes históricos. O limite
de três bytes por mensagem e o limite de dois bytes de RX cobrem somente
este diagnóstico. O endereço SPI `0x00A8` tem contrato independente do
opcode MCU `A8` e da consulta de Chip ID.

O prazo monotônico de 1 a 60.000 ms começa após adquirir a sessão. A API
conserva, por chamada ao executor, o status, o comprimento total
informado e o RX confirmado. Retorno zero exige ambos os comprimentos
exatos e a referência `00 A1`. `FF FF` é preservado no resultado com
status zero do executor e retorno diagnóstico `-EBADMSG`. Divergência de
comprimento com status zero retorna `-EMSGSIZE`. Remoção, cancelamento
e expiração têm precedência, nessa ordem, no resultado da sessão. Os
recibos conservam os erros originais mesmo quando esse resultado muda.
Se o executor informar RX acima da quantidade solicitada, o recibo
publicado zera o comprimento e o buffer RX e conserva o status do executor
e o comprimento total informado.

O cancelamento afeta a operação ativa. A remoção fecha a sessão e
drena o executor. A camada que detém o cliente deve bloquear novas
chamadas e esperar os chamadores existentes antes de liberar a sessão
ou o cliente. O prazo e o cancelamento são cooperativos. Uma mensagem
já admitida pode terminar depois do sinal. `spi_sync` depende dos
mecanismos de timeout do controlador e pode permanecer bloqueado além
do prazo lógico.

## Adaptador Linux desabilitado

O ponto de entrada retorna `-EOPNOTSUPP` por uma constante de compilação.
O Kbuild permite compilar objetos para revisão. A futura associação
deve receber do bind o `struct spi_device` enumerado a partir do recurso
SPI de FPNT. O adaptador confere o companion ACPI (HID e caminho
normalizado), o ancestral PCI Intel `8086:34ab` e o perfil de cliente
revisado (CS 0, modo 0, 8 bits, teto de 10 MHz). A origem ACPI do recurso
(`\_SB.PCI0.SPI1`) deve ser conferida contra a associação feita pelo
kernel alvo antes de habilitar o caminho. Índices globais de barramento
ficam fora da associação. A flash `spi1.0` fica excluída do plano.

O caminho em revisão converte cada mensagem lógica em uma `spi_message`,
verifica `spi_max_message_size` e `spi_max_transfer_size` e usa buffers
alocados separadamente para TX e RX. Dentro da segunda mensagem os dois
`spi_transfer` usam `cs_change = 0`. Os campos de velocidade, tamanho de
palavra e atraso dos transfers ficam nos defaults do cliente. O caminho
chama `spi_sync` uma vez por mensagem e usa `actual_length`. RX só é
publicado após sucesso com comprimento total exato, até haver base para
atribuir bytes parciais a uma fase.

## Validação offline

Na raiz do projeto:

```sh
make -C tests/spi test sanitize
```

As duas variantes usam o mesmo núcleo C com mutexes POSIX no host. Os
testes exercitam somente o executor sintético. A compilação dos objetos
confere as APIs de kernel usadas no adaptador. A suíte KUnit de pesquisa
continua cobrindo o protocolo MCU, staging, eventos e o próprio ciclo de
vida, com [resultados preservados](../../docs/evidence/README.md).
