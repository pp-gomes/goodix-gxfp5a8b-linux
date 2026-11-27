# Estado funcional

A prioridade é o transporte SPI 0.3. As leituras diagnósticas históricas produziram `FF FF` no Linux e `00 A1` na referência Windows. A causa é desconhecida.

| Marco | Estado |
|---|---|
| 0.1 | Cliente ACPI/SPI reconhecido historicamente. Falta log de bind original vinculado ao hash de fonte |
| 0.2 | GPIO e IRQ parcialmente documentados. Função elétrica, polaridade e ownership de GFPS ainda abertos |
| 0.3 | Provas experimentais Linux sem resposta interpretável. 0.3-A: transporte C exercitado com executor sintético e adaptador Linux desabilitado. 0.3-B: resposta válida pendente |
| 0.4 | Eventos sintéticos testados. Falta recepção real do MCU |
| 0.5 | Consulta de Chip ID pendente |
| 0.6 | Inicialização segura pendente |
| 0.7 | Cadastro e verificação no dispositivo pendentes |
| 1.0 | Integração libfprint pendente |

A identidade histórica é `\_SB.PCI0.SPI1.FPNT`, HID `GXFP5A8B`, cliente `spi-GXFP5A8B:00`, controlador `pxa2xx-spi.3`. GFPS e GFPI aparecem nos registros como pinos ACPI 46 e 259, respectivamente. Índices SPI, IRQ e GPIO dependem do host e do boot. A [comparação autenticada dos fontes Ubuntu](../src/README.md#comparação-dos-fontes-ubuntu--42-e--43) encontrou igualdade byte a byte nos 26 arquivos revisados entre `7.0.0-42-generic` e `7.0.0-43-generic`. O [caminho do CS nativo](V03_SPI_TRANSPORT_GAPS.md#cs-nativo-e-modos-históricos) é coerente com o perfil declarado.

A [evidência KUnit](evidence/README.md) cobre 25 casos por perfil em Basic, lockdep, KASAN e KCSAN Strict. O [roadmap](ROADMAP.md) e os [critérios de transporte](V03_SPI_TRANSPORT_GAPS.md) definem a aceitação física.
