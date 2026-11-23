# Goodix GXFP5A8B — Diagnóstico ACPI/SPI e observador opcional de IRQ (v0.3)

Compatibilidade visada: LG Gram `14Z90N-V.BR51P1`, ACPI HID `GXFP5A8B`.

## Descobertas no computador do usuário

- `spi-GXFP5A8B:00` / `pxa2xx-spi.3`, CS0, modo 0, 8 bits, 10 MHz.
- Recurso ACPI `GpioIo` de saída: pino **46**, controlador `\\_SB.GPI0`.
- Recurso ACPI `GpioInt`: pino **259**, edge, active-high, wake capable.
- `spi->irq`: **176**, `intel-gpio` `hwirq=259`.
- Sem handler anterior; IRQ `176` não aparece em `/proc/interrupts` quando o driver não a requisita.

## Segurança e limites

**Modo padrão (`observe_irq=0`)**: obtém atributos SPI, inspeciona `_CRS`
(AML executado pelo firmware) e cria atributo sysfs de contagem igual a zero.
Não requisita IRQ, não executa transferência SPI e não solicita nem altera GPIO.

**Modo opcional (`observe_irq=1`)**: adicionalmente registra um handler para
`spi->irq` usando as configurações do kernel (sem redefinir trigger) e conta
interrupções. **Requisitar uma IRQ altera o estado de mascaramento do controlador**
e pode produzir um fluxo contínuo de interrupções ou outros efeitos imprevistos.
Só faça esse teste após salvar seu trabalho; ele não é tão passivo quanto o
modo padrão. O handler não lê estado da digital; retorna `IRQ_HANDLED` e conta.

NÃO há função de reset, firmware update, PSK, TLS, `spi_sync`, gpio write,
`gpiod_get`, `spi_setup`, requisição de GPIO ou `spidev`.

## Compilar

```bash
uname -r
ls -ld /lib/modules/$(uname -r)/build
make clean
make
```

Headers do kernel precisam coincidir com `uname -r`. O código foi compilado
apenas com headers Linux 6.12.107 (ambiente de referência), não no kernel
7.0.0-42-generic do computador do usuário.

## Testar primeiro sem solicitar IRQ

```bash
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
if [ -L "$SPI/driver" ]; then echo "driver já associado; não carregue"; else echo "sem driver"; fi
sudo insmod ./gxfp5a8b_probe.ko
sudo dmesg | grep -i gxfp5a8b | tail -n 25
cat "$SPI/irq_events"
sudo rmmod gxfp5a8b_probe
```

## Testar opcionalmente o contador de IRQ

Antes: salve trabalhos e confirme que o driver está descarregado. Não execute
esse teste em ambientes críticos. Há risco de travamento ou IRQ storm.

```bash
sudo insmod ./gxfp5a8b_probe.ko observe_irq=1
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
cat "$SPI/irq_events"
grep -E '^[[:space:]]*176:' /proc/interrupts
# Se desejar, toque uma vez no leitor, sem cadastrar digital, e compare:
cat "$SPI/irq_events"
sudo rmmod gxfp5a8b_probe
sudo dmesg | grep -i gxfp5a8b | tail -n 30
```

Se o contador permanecer em zero, **não** significa necessariamente que a IRQ ou
o hardware estejam com defeito: pode ser preciso inicializar/ativar o sensor
antes de produzir eventos. Se ocorrer tempestade de interrupções (contador
subindo continuamente) ou instabilidade, remova o módulo assim que for seguro.

## Falhas de carregamento

Secure Boot pode recusar módulos não assinados. Verifique `sudo dmesg | tail -n 50`.
Não desligue Secure Boot apenas para esse teste.

O teste não comprova resposta SPI, leitura de ID, captura ou funcionamento de
impressão digital. Ainda precisamos portar a camada de transporte, manipulação
de GPIO e sequências de inicialização não destrutivas.
