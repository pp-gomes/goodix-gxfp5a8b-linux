# Goodix GXFP5A8B — diagnóstico SPI experimental v0.5

**NÃO é driver de impressão digital. NÃO está validado em hardware.**
Notebook alvo: LG Gram 14Z90N-V.BR51P1; identificador ACPI `GXFP5A8B`.

## Origem da mudança

Na análise estática do `Wbdi.dll`, a rotina Windows Geneva `WakeUp`
(`0x18004afe0`) emite um byte SPI bruto `E5` e espera 50 ms. A leitura
`McuGetCpuVersion` (`0x18004d970`) em seguida seleciona endereço SPI `00A8`
com `F0 00 A8`, depois realiza `F1`+RX2. A v0.4 repetiu apenas a leitura,
concluiu a transferência sem erro de controlador mas recebeu `FF FF`.
A hipótese de que falta E5 ainda **não está comprovada**.

Esta v0.5 isola somente essa variável e permite comparação.

## Controles de segurança

- Por padrão, `allow_cpu_read=0`, `allow_wakeup_cpu_read=0`, `observe_irq=0`.
  **Carregar módulo não envia bytes SPI.**
- Modos mutuamente exclusivos; combinações são rejeitadas no `probe()`.
- O atributo `wakeup_cpu_version` só existe com `allow_wakeup_cpu_read=1`,
  tem modo `0400` e transfere SPI apenas quando **root lê explicitamente**.
- Uma tentativa por carregamento (inclusive se a primeira fase falhar),
  sem retry, sem fallback, sem reset ou operações GPIO.
- Verifica modo0, 8 bits, 10 MHz, barramento0 e CS0 antes do I/O.
- O caminho `cpu_version` sem E5 da v0.4 continua disponível **se** carregado
  com `allow_cpu_read=1`, para comparação, mas não é necessário repeti-lo.
- `observe_irq=1` apenas registra o handler já testado; não combinar com I/O SPI.
- **Não acessa** SPI NOR em `spi1.0`, não modifica parâmetros SPI nem pinos.

O byte E5 é operação com efeito de estado pretendido (*wakeup*). A análise
estática não exclui efeitos colaterais do firmware; módulos de kernel também
podem causar instabilidade. Com a BIOS `Disabled`, não sabemos se o Goodix
está alimentado ou fora de reset. Não existe garantia de inocuidade.

## Arquivos

- `gxfp5a8b_probe.c` — fonte do módulo.
- `Makefile` — compilação externa.
- `test_frames_offline.py` — testes locais, sem hardware.
- `compilacao-referencia.txt` — resultado de build no ambiente de referência.

## 1. Verificação offline, sem I/O SPI

```bash
cd ~/Downloads
unzip GXFP5A8B_v05_E5_cpu_diagnostico.zip
cd Goodix_GXFP5A8B_v05
find . -maxdepth 1 -type f -exec touch {} +
python3 -m unittest -v test_frames_offline.py
make clean && make
```

Compilar **no kernel ativo do LG Gram**. O build do pacote foi realizado
contra headers de referência 6.12.107, não contra 7.0.0-42-generic.

```bash
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
if [ -L "$SPI/driver" ]; then
  echo 'Outro driver associado; pare aqui.'
else
  sudo insmod ./gxfp5a8b_probe.ko \
       observe_irq=0 allow_cpu_read=0 allow_wakeup_cpu_read=0
  sudo dmesg | grep gxfp5a8b | tail -n 15
  sudo rmmod gxfp5a8b_probe
fi
```

## 2. Teste ATIVO OPCIONAL: **um** E5 e **uma** consulta de CPU

Somente após validar build e modo passivo, salvar o trabalho e decidir
explicitamente aceitar os riscos. Mantenha a BIOS na configuração escolhida,
registrando qual é (no nosso caso, `Disabled`). Não altere GPIO 46/259.

```bash
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
if [ -L "$SPI/driver" ]; then
    echo 'Dispositivo ocupado: não carregar módulo'
else
    sudo insmod ./gxfp5a8b_probe.ko \
        observe_irq=0 allow_cpu_read=0 allow_wakeup_cpu_read=1
    # ESTA LEITURA é o momento em que E5 e F0/F1 serão transmitidos:
    sudo cat "$SPI/wakeup_cpu_version"
    sudo dmesg | grep -iE 'gxfp5a8b|spi.*error|pxa2xx' | tail -n 25
    sudo rmmod gxfp5a8b_probe
fi
```

Transferências esperadas para esse teste único:

| Mensagem | Transferências | TX explícito | RX |
|---|---:|---|---|
| WakeUp | 1 | `E5` | nenhum |
| Selecionar CPU | 1 | `F0 00 A8` | nenhum |
| Ler CPU | 2 | `F1` | 2 bytes |

Total: **3 mensagens, 4 transferências, 5 bytes TX e 2 bytes RX**.
Isso representa **7 bytes de contagem agregada** nas estatísticas do kernel.
O RX precisa de clocks, mas o valor TX4/TX5 não soma os clocks da fase RX.

Saída de sucesso da interface (somente exemplo de formato):

```text
wakeup=1 raw=12 34 be16=0x1234 status=unvalidated
```

`00 00`, `ff ff` ou qualquer outro valor não são automaticamente uma versão
válida. Se houver erro, não repetir, não tentar reset, PSK, firmware ou FDT.
O atributo é de **leitura com efeitos**; uma segunda leitura devolve `EALREADY`.

## Fontes técnicas

- Relatório Codex `gpio-reset-wakeup-investigation.md`, seções 1–6.
- Linux SPI documentation: https://docs.kernel.org/7.0/driver-api/spi.html
