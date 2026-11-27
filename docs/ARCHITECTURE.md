# Arquitetura e decisões

**Transporte 0.3.** A sessão em [src/](../src/README.md) serializa a seleção de endereço e a leitura como um único diagnóstico lógico. O contrato registra status, comprimento efetivo e RX por mensagem. O backend em memória exercita respostas, falhas, cancelamento e remoção.

**Protocolo e concorrência.** O [núcleo MCU](../research/offline-mcu/README.md) implementa formatação, checksums e parsing em C. A [suíte KUnit](../research/offline-kunit/README.md) exercita o núcleo adaptado para tipos do kernel, staging e troca de mensagens com eventos sintéticos. A fronteira de transporte do KUnit retorna `GX_HARDWARE_DISABLED`.

**Identidade e pré-condições.** A instância precisa vir do recurso ACPI do cliente Goodix. O [adaptador Linux desabilitado](../src/spi/README.md) verifica HID, caminho ACPI, ancestral PCI e perfil SPI. A integração futura deve confirmar que o controlador enumerado corresponde à origem ACPI do recurso. A [auditoria Ubuntu](../src/README.md#comparação-dos-fontes-ubuntu--42-e--43) cobre CS, status, comprimento, timeout e a retomada da pilha SPI/LPSS nas duas revisões examinadas. A matriz de [lacunas SPI](V03_SPI_TRANSPORT_GAPS.md) registra as hipóteses e seus limites.

**Histórico e evidências.** Os experimentos anteriores ficam em [historical/](../historical/README.md). Os [resultados KUnit](evidence/README.md) mantêm a proveniência e o escopo offline.
