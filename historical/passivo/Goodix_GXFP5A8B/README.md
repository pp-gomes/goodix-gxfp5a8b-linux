# GXFP5A8B — módulo de diagnóstico SPI passivo (experimental)

**Não é um driver biométrico.** Seu único propósito é confirmar, no LG Gram
14Z90N-V.BR51P1, que um `spi_driver` Linux pode fazer *bind* com o dispositivo
ACPI `GXFP5A8B`, registrando atributos SPI/IRQ sem acessá-los ativamente.

## O que faz
- Registra ACPI HID `GXFP5A8B` em `acpi_match_table`.
- Faz bind com um `spi_device` enumerado pelo próprio kernel.
- Imprime bus, chip select, modo, bits/word, frequência máxima e `spi->irq`.
- Avisa se os parâmetros diferem do perfil informado no DSDT do notebook.

## O que **não** faz
- **Não** executa `spi_sync`, `spi_write`, `spi_read`, `spi_setup` ou outras transferências.
- **Não** solicita GPIO, modifica nível de pino, faz reset ou requisita IRQ.
- **Não** cria `/dev/spidev*`, não lê ID real do chip, não cadastra digitais.
- **Não** carrega firmware, altera PSK ou inicia TLS.

A associação de um driver pode ter efeitos indiretos de gerenciamento de dispositivos
no sistema; este código, por si, não emite I/O SPI nem mexe em pinos.

## Compilar (na máquina LG Gram, não em outro computador)

No Ubuntu, verifique se os headers do kernel ativo existem:

```bash
uname -r
ls -ld /lib/modules/$(uname -r)/build
```

Se a pasta não existir, tente instalar os headers correspondentes aos repositórios
configurados, por exemplo `sudo apt install linux-headers-$(uname -r)`. Se os
headers não estiverem disponíveis, não use headers de outra versão para compilar.

```bash
make
```

## Testar

Antes de carregar, confirme que o dispositivo não possui outro driver:

```bash
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
[ ! -e "$SPI/driver" ] && echo 'sem driver' || readlink -f "$SPI/driver"
```

Se estiver livre e você concordar com a instalação de um módulo experimental:

```bash
sudo insmod ./gxfp5a8b_probe.ko
sudo dmesg | grep -i 'gxfp5a8b' | tail -n 30
readlink -f /sys/bus/spi/devices/spi-GXFP5A8B:00/driver
```

Espera-se `.../drivers/gxfp5a8b_probe` no último comando se houve bind. A saída
`irq` pode ser zero ou negativa mesmo havendo `GpioInt` no ACPI: não é um teste
conclusivo sobre a interrupção do leitor. O teste **não** comprova que o sensor
responde a comandos SPI.

Para remover:

```bash
sudo rmmod gxfp5a8b_probe
```

O Secure Boot pode impedir `insmod` de um módulo sem assinatura. Não é
recomendado desativar Secure Boot apenas para esse teste; faça primeiro o
diagnóstico do erro e avalie assinatura/MOK se necessário.

## Limitações

- Escrito com a API SPI documentada para kernels recentes. A compilação foi
  verificada em ambiente de referência com headers 6.12; **não** foi compilado
  nem executado no kernel 7.0.0-42-generic do usuário.
- O DSDT fornece o perfil SPI de 10 MHz, modo 0, CS0 e 8 bits. O software
  registra os valores de `struct spi_device` sem reconfigurá-los.
- Não documenta números definitivos GPIO nem traduz um recurso ACPI GPIO
  para `gpiod` na aplicação. A análise de `SHPO`, `GFPI` e `GFPS` continua.
- O módulo só deve ser testado na máquina cujo ACPI contém `GXFP5A8B`.

## Fontes
- https://docs.kernel.org/7.0/driver-api/spi.html
- https://docs.kernel.org/firmware-guide/acpi/enumeration.html
