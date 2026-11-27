# Diagnóstico Windows do controlador Intel SPI

Copie `collect-controller-config.ps1` para uma pasta pessoal no Windows. Abra o Windows PowerShell 5.1 e, na pasta do script, execute:

```powershell
.\collect-controller-config.ps1 -OutputDirectory "$env:USERPROFILE\Goodix-Diagnostico"
```

O script imprime os caminhos completos. Envie ao ChatGPT o arquivo
`%USERPROFILE%\Goodix-Diagnostico\goodix-controller-config_*_UTC.md` mais
recente. O JSON com o mesmo nome-base é opcional e serve para comparação
automatizada. Os dois arquivos contêm os mesmos campos de diagnóstico.
Use `-EventDays 7`, por exemplo, para ajustar a janela de eventos.

Uma sessão comum pode reportar alguns campos como `inacessivel`. Se o
PowerShell bloquear especificamente um arquivo baixado por marca de origem,
revise o conteúdo e, se permitido no seu ambiente, use
`Unblock-File -LiteralPath .\collect-controller-config.ps1` apenas nesse
arquivo. Respeite as políticas de execução gerenciadas. A coleta usa os
módulos nativos do Windows, sem instalação de ferramentas.

## Leitura dos resultados

| Fonte | Evidência e uso na comparação |
| --- | --- |
| PnP | PCI 8086:34AB com subsistema e revisão quando codificados no ID, ACPI GXFP5A8B, versão, INF publicado, serviço e código de problema. Quando há mais de uma instância presente para um papel, a consulta dependente daquela identidade fica indisponível. Ter os dois dispositivos presentes não estabelece a associação SPB do boot. |
| Serviço Intel | Estado do serviço, dependência SpbCx e SHA-256 do SYS no caminho reportado pelo serviço quando o caminho é reconhecido. O SHA-256 identifica o arquivo em disco. O INF publicado pode variar em outra instalação. |
| Registro do serviço | `SYSTEM\CurrentControlSet\Services\<serviço>\Parameters` é a chave de parâmetros KMDF. O binário revisado abre essa chave e lê os 11 nomes permitidos no formato `Nome_<índice decimal>` como `REG_DWORD`. Os presentes são candidatos por índice. O índice PCI interno vem de uma leitura de configuração feita pelo driver. A coleta administrativa não resolve esse índice. A ausência indica que não foi encontrado um valor nomeado no escopo lido e não exclui configuração pela plataforma. |
| Energia PnP e WDF | `DEVPKEY_Device_PowerData` fornece capacidades e último estado reportado. A opção persistente `WdfDirectedPowerTransitionEnable` é consultada na subchave WDF do controlador. Os timeouts Intel de monitor ligado/desligado, quando encontrados, são expressos em ms. Compare com a política de autosuspend Linux sem equiparar seus escopos. |
| Canais e eventos | Debug e Performance Intel e os canais System e Kernel-PnP/Configuration são verificados por metadados. Até 20 eventos PnP de configuração, início e falha de início por dispositivo, dos últimos 30 dias por padrão, filtrados pelo ID exato, são reduzidos a ID e minuto UTC. Os canais Intel ficam restritos à consulta de existência e estado. |
| Conexão | O perfil histórico de referência da plataforma identificada é rotulado como tal. O descritor SPB do boot, a configuração programada nos registradores e o clock, CS e alimentação durante o pedido exigem evidência adicional. |

Os parâmetros persistentes considerados são `SourceClockFrequency` (Hz),
`DividerMValue`, `LoopbackMode`, `MosiStateOnRead`, `DynamicClockGating`,
`MonitorOnIdleTimeout`, `MonitorOffIdleTimeout`, `DmaInit`,
`DmaThreshold`, `CsSetupTime` e `CsHoldTime`. Defaults iniciais ficam em
seção própria e só aparecem com o SHA-256 do SYS analisado. Variantes,
configurações da plataforma e o estado da sessão podem alterar os valores
aplicados.

A saída usa uma lista fechada de campos. IDs de instância são usados dentro
do processo para selecionar os dispositivos e restringir a consulta de
eventos. Caminhos de serviço, nomes de máquina e usuário, IDs individuais,
mensagens e payloads de eventos ficam fora dos arquivos. A coleta usa
consultas administrativas e escreve apenas os dois relatórios no destino
escolhido.

[Análise 0.3](../../docs/V03_SPI_TRANSPORT_GAPS.md#controlador-intel-spi-no-windows) · [Chaves WDF](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/introduction-to-registry-keys-for-drivers) · [CM_POWER_DATA](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-cm_power_data_s) · [Directed Power Management](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-the-directed-power-management-framework)

## Teste sintético

Execute a partir da raiz do repositório em Windows PowerShell 5.1 ou PowerShell 7:

```powershell
& ./tests/windows/test-collect-controller-config.ps1
```

O teste usa mocks de PnP, serviço, parâmetros de registro, hash e eventos.
A sintaxe e o relatório são verificados sem chamar APIs do Windows.
