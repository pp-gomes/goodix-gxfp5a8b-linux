# GXFP5A8B — diagnóstico ACPI/SPI v0.2

Somente associa ao dispositivo `ACPI\\GXFP5A8B`, imprime `struct spi_device` e percorre os descritores do método ACPI `_CRS`: `SpiSerialBus`, `GpioIo` e `GpioInt`.

**Não é um driver biométrico** e não transfere bytes no SPI, não requisita uma IRQ, não manipula GPIO e não realiza reset ou atualização de firmware. A invocação do método ACPI `_CRS` avalia código AML do fabricante; embora seja um método de consulta, efeitos colaterais do firmware não podem ser excluídos completamente. A associação de driver também envolve as rotinas normais do kernel. Execute apenas no hardware indicado e após salvar seu trabalho.

## Teste no LG Gram (kernel 7.0.0-42-generic)

```bash
cd ~/Downloads/gxfp5a8b_acpi_probe_v02
ls -ld /lib/modules/$(uname -r)/build
make clean && make
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
if [ -L "$SPI/driver" ]; then echo 'ERRO: dispositivo possui driver'; readlink -f "$SPI/driver"; else echo 'Sem driver associado: pode prosseguir'; fi
```

Se o dispositivo continuar sem driver, carregar e ler logs:

```bash
sudo insmod ./gxfp5a8b_probe.ko
sudo dmesg | grep -E 'gxfp5a8b_probe|ACPI Gpio|ACPI SpiSerialBus' | tail -n 40
readlink -f /sys/bus/spi/devices/spi-GXFP5A8B:00/driver
sudo rmmod gxfp5a8b_probe
```

Se `insmod` informar `File exists`, confirme se o módulo anterior ainda está carregado (`lsmod | grep gxfp5a8b`) e se não há outro driver associado; não force `bind`/`unbind`. Se o Secure Boot bloquear o módulo, não desative o Secure Boot apenas para o teste.

O campo `pin[n]` mostra o **pin number do descritor ACPI**. Não é garantido que corresponda diretamente ao offset exibido pelo `gpioinfo` ou ao `hwirq` da IRQ. Não tente configurar manualmente essas linhas.

Código validado quanto à compilação com headers Linux **6.12.107** em ambiente de referência; ainda **não** validado no kernel Ubuntu **7.0.0-42-generic** ou no hardware físico.
