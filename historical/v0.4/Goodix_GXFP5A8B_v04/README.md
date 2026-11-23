# Goodix GXFP5A8B — v0.4 (sonda SPI de diagnóstico, experimental)

> **Não é driver de autenticação biométrica.** Código experimental de kernel para
> **LG Gram 14Z90N-V.BR51P1**, ACPI `GXFP5A8B`. É derivado do módulo de inspeção
> v0.3 e da engenharia reversa estática reportada em `minimal-initialization.md`.

## O que mudou

- Preserva o inventário passivo ACPI/SPI e contador IRQ opcional da v0.3.
- Adiciona diagnóstico **opcional, root-only e one-shot** da função de leitura
  direta de versão da CPU (`McuGetCpuVersion`, função Windows em `0x18004d970`).
- Por padrão `allow_cpu_read=0`: módulo **não envia bytes ao SPI**.
- Para habilitar o teste, é necessário carregar com `allow_cpu_read=1` e,
  separadamente, ler `/sys/bus/spi/devices/spi-GXFP5A8B:00/cpu_version`
  como root. Apenas nessa leitura é feito I/O SPI.
- Primeiro frame `F0 00 A8` (seleção de endereço); segunda transação
  `F1` seguida de leitura de 2 bytes, usando `spi_write_then_read`.
- Impede executar mais de **uma** tentativa por carregamento, inclusive se
  ocorrer erro. Não há retries, reset nem fallback.
- Confere bus0, CS0, 8 bits, modo0 e frequência 10 MHz antes de transmitir.

**Nenhuma operação altera GPIO**, requisita GPIO de saída, executa WakeUp
`E5`, consulta MCU `0x82`, aciona PSK/TLS, modifica firmware, arma detecção
FDT ou depende da IRQ para o diagnóstico da CPU.

**Atenção:** mesmo uma transação que pretende ler um registrador pode afetar
estado interno do dispositivo. Não há validação elétrica/firmware da sequência
no GXFP5A8B físico. Não execute a parte ativa se o risco não for aceitável.

## 1. Descompactar e compilar no notebook

```bash
cd ~/Downloads
unzip Goodix_GXFP5A8B_probe_v04.zip
cd Goodix_GXFP5A8B_v04

# Ajusta possíveis timestamps do zip, sem alterar código
find . -maxdepth 1 -type f -exec touch {} +

ls -ld /lib/modules/$(uname -r)/build
make clean
make
```

Esperado: `gxfp5a8b_probe.ko`. O pacote inclui apenas fontes:
**nunca use um `.ko` compilado para outro kernel**.

## 2. Teste passivo (recomendado antes de qualquer leitura)

```bash
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
if [ -L "$SPI/driver" ]; then
  echo "Já existe driver associado; não carregue este módulo"
else
  sudo insmod ./gxfp5a8b_probe.ko observe_irq=0 allow_cpu_read=0
  sudo dmesg | grep gxfp5a8b | tail -n 18
  cat "$SPI/irq_events"
  sudo rmmod gxfp5a8b_probe
fi
```

Nesse modo, só são consultadas informações do kernel e os recursos ACPI;
nenhuma transação SPI é enviada pelo código do módulo.

## 3. Diagnóstico ATIVO OPCIONAL: ler dois bytes da CPU

**Execute apenas se tiver decidido prosseguir com uma transferência ao hardware.
O teste não foi executado pelos autores no LG Gram e pode falhar com timeout.**
Salve seu trabalho primeiro. Confirme que o dispositivo não está ocupado.

```bash
SPI=/sys/bus/spi/devices/spi-GXFP5A8B:00
if [ -L "$SPI/driver" ]; then
  echo 'Dispositivo ocupado: não carregue o módulo'
else
  echo 'Dispositivo disponível'
fi
# Prossiga somente se o resultado foi 'Dispositivo disponível'.

# Ainda não envia nenhum byte pelo SPI:
sudo insmod ./gxfp5a8b_probe.ko observe_irq=0 allow_cpu_read=1

# PRIMEIRA (e única) solicitação de leitura de hardware:
sudo cat "$SPI/cpu_version"

# Logs de erro de transporte, se existirem:
sudo dmesg | grep -iE 'gxfp5a8b|spi.*error' | tail -n 30

# Limpeza:
sudo rmmod gxfp5a8b_probe
```

Se a leitura der certo, a saída tem formato:

```text
raw=ab cd be16=0xabcd
```

`ab cd` é apenas **exemplo de formato**, não resultado esperado nem confirmação
de identificação. Bytes `00 00` ou `ff ff` podem representar estado não pronto,
resposta vazia ou protocolo não compatível; não inferir Chip ID a partir deles.
Se houver erro (`Input/output error`, timeout etc.) **não repetir em loop**,
não alternar GPIO 46, não tentar reset, E5 ou comandos de firmware.
Uma segunda leitura do atributo na mesma carga retorna `-EALREADY`.

## Segurança da execução

- O driver experimental faz bind no dispositivo ACPI `GXFP5A8B`; outros
  dispositivos SPI, inclusive o SPI-NOR, **não** devem ser tocados.
- Não combine `observe_irq=1` com `allow_cpu_read=1`: probe rejeita a combinação.
- A versão 0.4 foi **compilada**, não executada no sensor real. A compilação foi
  validada com headers Linux `6.12.107+deb13-amd64`, não com os headers do
  Ubuntu `7.0.0-42-generic`.
- Módulos fora da árvore podem registrar avisos de assinatura e *taint*.
  Não recomendamos desabilitar Secure Boot somente por causa deste teste.

## Por que não enviar `0x82` agora?

O opcode MCU `0x82` requer caminho IOHub, seleção de mailbox `0x3D00`,
recepção de ACK/dados via IRQ e validação de pacote. O comando `E5` pode ser
emitido implicitamente em determinados estados do driver Windows, mas seu
resultado no hardware atual é desconhecido. A leitura direta SPI `0x00A8` é um
passo diagnóstico menor, ainda com risco de I/O ativo.

Fontes para a API de transporte Linux:
https://docs.kernel.org/7.0/driver-api/spi.html

Engenharia reversa: `minimal-initialization.md`, especialmente o item
"SelfCheck e McuGetChipId" e o mapa de comandos/endereço.
